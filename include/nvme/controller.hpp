#pragma once

#include "../dma/heap.hpp"
#include "../os/mmio.hpp"
#include "nvme/command.hpp"
#include "nvme/controller_configuration.hpp"
#include "nvme/controller_state.hpp"
#include "nvme/queue.hpp"
#include "nvme/queue_entry.hpp"
#include "registers.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace nvme {

using namespace std::chrono_literals;

/// @brief A lightweight NVME device handle that contains the BAR0 region
struct NvmeDeviceHandle {
    NvmeDeviceHandle(os::MmioRegion bar0, dma::DmaHeap& dma) : bar0{bar0}, dma{&dma} {
    }

    os::MmioRegion bar_region() const {
        return bar0;
    }

    dma::DmaHeap& dma_heap() const {
        assert(dma != nullptr);
        return *dma;
    }

private:
    os::MmioRegion bar0;
    dma::DmaHeap* dma;
};

/// @brief NVMe controller class that is used to interface with the NVMe device controller
class Controller {
private:
    static constexpr std::uint8_t CtlEn = 0b1;
    static constexpr std::uint8_t CtlDis = 0b0;

public:
    explicit Controller(NvmeDeviceHandle device);

    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    bool enable();

    bool disable();

    [[nodiscard]] Result<void> configure();

    const ControllerConfiguration& configuration() const;

    // submits an admin command and blocks until its own completion arrives
    template<CommandDescriptor Command>
    Result<typename Command::Completion> execute(const Command& command) {
        return submit_admin_command(command).and_then([](auto&& pending) {
            return pending.result();
        });
    }

    template<DataCommand Command>
    Result<typename Command::Data> execute(const Command& command) {
        const std::size_t size = command.data_size();
        dma::DmaBuffer buffer = heap_->alloc(size, configuration_.nvme_page_size);
        const dma::DmaView data{buffer};

        auto pending = submit_to<Command>(
            constants::ADMIN_QID,
            [&](std::uint16_t cid) noexcept {
                return command.build(cid, data);
            },
            std::move(buffer)
        );
        if (!pending) {
            return std::unexpected{std::move(pending.error())};
        }

        return pending->wait()
            .and_then([](const wire::CompletionEntry& completion) {
                return succeeded(Command::name, completion);
            })
            .transform([&](const wire::CompletionEntry&) {
                return command.decode(std::span<const std::byte>{data.uva, size});
            });
    }

    // submits an io command to the queue pair qid and blocks until its own completion arrives
    template<TransferCommand Command>
    Result<dma::DmaBuffer> execute(QueueId qid, const Command& command, dma::DmaBuffer buffer) {
        auto pending = submit_io_command(qid, command, std::move(buffer));
        if (!pending) {
            return std::unexpected{std::move(pending.error())};
        }

        return pending->wait()
            .and_then([](const wire::CompletionEntry& completion) {
                return succeeded(Command::name, completion);
            })
            .transform([&](const wire::CompletionEntry&) {
                return std::move(*pending->buffer_);
            });
    }

    // pushes the command into the admin SQ and returns a CommandPending handle tied to this command's type
    template<CommandDescriptor Command>
    Result<CommandPending<Command>> submit_admin_command(const Command& command) {
        return submit_to<Command>(
            constants::ADMIN_QID,
            [&](std::uint16_t cid) noexcept {
                return command.build(cid);
            },
            std::nullopt
        );
    }

    template<TransferCommand Command>
    Result<CommandPending<Command>> submit_io_command(QueueId qid, const Command& command, dma::DmaBuffer buffer) {
        if (io_queue(qid) == nullptr) {
            return std::unexpected{Error{
                .reason = std::format("IO queue {} does not exist on this controller", qid.value),
                .severity = Error::Severity::ERROR
            }};
        }

        const dma::DmaView data{buffer};
        return submit_to<Command>(
            qid,
            [&](std::uint16_t cid) noexcept {
                return command.build(cid, data);
            },
            std::move(buffer)
        );
    }

    const Queue& admin_queue() const;
    const Queue* io_queue(QueueId qid) const;

    std::size_t io_queue_count() const;
    std::uint32_t max_io_queue_entries() const;

    bool requires_contiguous_queues() const;

    // sets controller's state after an Identify Controller command is issued
    void set_state(ControllerState state);

    const std::optional<ControllerState>& state() const;

    // set number of IO queues to use in the controller
    void grant_io_queues(std::uint16_t count);

    // allocates a queue id and the host memory backing that queue pair
    Result<QueueId> add_io_queue_pair(std::size_t depth);

    void remove_io_queue_pair(QueueId qid);

private:
    template<typename Command>
    friend class CommandPending;

    template<typename Command, std::invocable<std::uint16_t> Build>
    Result<CommandPending<Command>> submit_to(QueueId qid, Build&& build, std::optional<dma::DmaBuffer> buffer) {
        return queue(qid)->submit(std::forward<Build>(build)).transform([&](Cid cid) {
            return CommandPending<Command>{*this, qid, cid, std::move(buffer)};
        });
    }

    Result<QueueId> allocate_io_qid();

    Queue* queue(QueueId qid);

    // blockingly polls the CQ until the completion for cid arrives or the timeout expires
    Result<wire::CompletionEntry> poll_command(QueueId qid, Cid cid);

    // non-blockingly drains whatever completions are visible on the cq
    std::optional<Result<wire::CompletionEntry>> try_poll_command(QueueId qid, Cid cid);

    void abandon_command(QueueId qid, Cid cid, std::optional<dma::DmaBuffer> buffer);

    bool wait_for(std::predicate auto&& predicate, std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }

            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    bool rdy_is(unsigned want);

    void fetch_timeout();

    // en = 0b0 -> disable
    // en = 0b1 -> enable
    bool change_controller_state_to(std::uint8_t en);

    //
    // Internal state
    //
    ControllerRegisters regs_;
    const ControllerConfiguration configuration_;
    dma::DmaHeap* heap_;

    std::vector<std::unique_ptr<Queue>> queues_;

    std::chrono::milliseconds timeout_;

    std::optional<ControllerState> state_;

    std::uint16_t io_qid_grant_{0};
};

template<typename Command>
CommandPending<Command>::CommandPending(CommandPending&& other) noexcept
    : controller_{std::exchange(other.controller_, nullptr)},
      qid_{other.qid_},
      cid_{other.cid_},
      completion_{other.completion_},
      buffer_{std::move(other.buffer_)} {
}

template<typename Command>
CommandPending<Command>& CommandPending<Command>::operator=(CommandPending&& other) noexcept {
    if (this != &other) {
        release();
        controller_ = std::exchange(other.controller_, nullptr);
        qid_ = other.qid_;
        cid_ = other.cid_;
        completion_ = other.completion_;
        buffer_ = std::move(other.buffer_);
    }
    return *this;
}

template<typename Command>
CommandPending<Command>::~CommandPending() {
    release();
}

template<typename Command>
Cid CommandPending<Command>::cid() const {
    return cid_;
}

template<typename Command>
QueueId CommandPending<Command>::queue_id() const {
    return qid_;
}

template<typename Command>
bool CommandPending<Command>::done() const {
    return completion_.has_value();
}

template<typename Command>
std::optional<Result<wire::CompletionEntry>> CommandPending<Command>::poll() {
    if (completion_.has_value()) {
        return completion_;
    }
    if (controller_ == nullptr) {
        return std::unexpected{empty_handle_diagnostic()};
    }

    auto polled = controller_->try_poll_command(qid_, cid_);
    if (polled.has_value() && polled->has_value()) {
        completion_ = **polled;
    }
    return polled;
}

template<typename Command>
Result<wire::CompletionEntry> CommandPending<Command>::wait() {
    if (completion_.has_value()) {
        return *completion_;
    }
    if (controller_ == nullptr) {
        return std::unexpected{empty_handle_diagnostic()};
    }

    auto waited = controller_->poll_command(qid_, cid_);
    if (waited.has_value()) {
        completion_ = *waited;
    }
    return waited;
}

template<typename Command>
auto CommandPending<Command>::result()
    requires CommandDescriptor<Command>
{
    return wait()
        .and_then([](const wire::CompletionEntry& completion) {
            return succeeded(Command::name, completion);
        })
        .transform([](const wire::CompletionEntry& completion) {
            return Command::decode_completion(completion);
        });
}

template<typename Command>
CommandPending<Command>::CommandPending(
    Controller& controller,
    QueueId qid,
    Cid cid,
    std::optional<dma::DmaBuffer> buffer
)
    : controller_{&controller}, qid_{qid}, cid_{cid}, buffer_{std::move(buffer)} {
}

template<typename Command>
void CommandPending<Command>::release() {
    if (controller_ != nullptr && !completion_.has_value()) {
        controller_->abandon_command(qid_, cid_, std::exchange(buffer_, std::nullopt));
    }
    controller_ = nullptr;
}

template<typename Command>
Error CommandPending<Command>::empty_handle_diagnostic() const {
    return Error{
        .reason = std::format("CommandPending for CID {} is empty (moved-from)", cid_),
        .severity = Error::Severity::ERROR
    };
}

}  // namespace nvme
