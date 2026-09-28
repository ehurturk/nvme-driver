#pragma once

#include "../dma/heap.hpp"
#include "../os/mmio.hpp"
#include "nvme/command.hpp"
#include "nvme/controller_configuration.hpp"
#include "nvme/controller_state.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/queue.hpp"
#include "registers.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
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
    Result<wire::CompletionEntry> execute_admin_command(Command cmd) {
        auto submitted = submit_admin_command(std::move(cmd));
        if (!submitted) {
            return std::unexpected{submitted.error()};
        }

        return submitted->wait();
    }

    // pushes the command into the admin SQ and returns a CommandPending handle tied to this command's type
    template<CommandDescriptor Command>
    Result<CommandPending<Command>> submit_admin_command(Command command) {
        return submit_to(constants::ADMIN_QID, std::move(command));
    }

    // submits an io command to the queue pair qid and blocks until its own completion arrives
    template<CommandDescriptor Command>
    Result<wire::CompletionEntry> execute_io_command(QueueId qid, Command cmd) {
        auto submitted = submit_io_command(qid, std::move(cmd));
        if (!submitted) {
            return std::unexpected{submitted.error()};
        }

        return submitted->wait();
    }

    template<CommandDescriptor Command>
    Result<CommandPending<Command>> submit_io_command(QueueId qid, Command command) {
        if (io_queue(qid) == nullptr) {
            return std::unexpected{Error{
                .reason = std::format("IO queue {} does not exist on this controller", qid.value),
                .severity = Error::Severity::ERROR
            }};
        }

        return submit_to(qid, std::move(command));
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
    Result<QueueId> add_io_queue_pair(dma::DmaHeap& heap, std::size_t depth);

    void remove_io_queue_pair(QueueId qid);

private:
    template<CommandDescriptor Command>
    friend class CommandPending;

    template<CommandDescriptor Command>
    Result<CommandPending<Command>> submit_to(QueueId qid, const Command& command) {
        return queue(qid)->submit(command).transform([&](Cid cid) {
            return CommandPending<Command>{*this, qid, cid};
        });
    }

    Result<QueueId> allocate_io_qid();

    Queue* queue(QueueId qid);

    // blockingly polls the CQ until the completion for cid arrives or the timeout expires
    Result<wire::CompletionEntry> poll_command(QueueId qid, Cid cid);

    // non-blockingly drains whatever completions are visible on the cq
    std::optional<Result<wire::CompletionEntry>> try_poll_command(QueueId qid, Cid cid);

    void abandon_command(QueueId qid, Cid cid);

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

    ControllerRegisters regs_;
    const ControllerConfiguration configuration_;

    std::vector<std::unique_ptr<Queue>> queues_;

    std::chrono::milliseconds timeout_;

    std::optional<ControllerState> state_;

    // qid management state
    std::uint16_t io_qid_grant_{0};
};

template<CommandDescriptor Command>
CommandPending<Command>::CommandPending(CommandPending&& other) noexcept
    : controller_{std::exchange(other.controller_, nullptr)},
      qid_{other.qid_},
      cid_{other.cid_},
      completion_{other.completion_} {
}

template<CommandDescriptor Command>
CommandPending<Command>& CommandPending<Command>::operator=(CommandPending&& other) noexcept {
    if (this != &other) {
        release();
        controller_ = std::exchange(other.controller_, nullptr);
        qid_ = other.qid_;
        cid_ = other.cid_;
        completion_ = other.completion_;
    }
    return *this;
}

template<CommandDescriptor Command>
CommandPending<Command>::~CommandPending() {
    release();
}

template<CommandDescriptor Command>
Cid CommandPending<Command>::cid() const {
    return cid_;
}

template<CommandDescriptor Command>
QueueId CommandPending<Command>::queue_id() const {
    return qid_;
}

template<CommandDescriptor Command>
bool CommandPending<Command>::done() const {
    return completion_.has_value();
}

template<CommandDescriptor Command>
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

template<CommandDescriptor Command>
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

template<CommandDescriptor Command>
Result<typename Command::Completion> CommandPending<Command>::result() {
    auto waited = wait();
    if (!waited.has_value()) {
        return std::unexpected{std::move(waited.error())};
    }

    if (!waited->ok()) {
        return std::unexpected{Error{
            .reason = std::format("CID {} completed with error status: {}", cid_, waited->status),
            .severity = Error::Severity::ERROR
        }};
    }

    return Command::decode_completion(*waited);
}

template<CommandDescriptor Command>
CommandPending<Command>::CommandPending(Controller& controller, QueueId qid, Cid cid)
    : controller_{&controller}, qid_{qid}, cid_{cid} {
}

template<CommandDescriptor Command>
void CommandPending<Command>::release() {
    if (controller_ != nullptr && !completion_.has_value()) {
        controller_->abandon_command(qid_, cid_);
    }
    controller_ = nullptr;
}

template<CommandDescriptor Command>
Error CommandPending<Command>::empty_handle_diagnostic() const {
    return Error{
        .reason = std::format("CommandPending for CID {} is empty (moved-from)", cid_),
        .severity = Error::Severity::ERROR
    };
}

}  // namespace nvme
