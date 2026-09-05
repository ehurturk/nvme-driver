#pragma once

#include "../dma/heap.hpp"
#include "../os/mmio.hpp"
#include "cid.hpp"
#include "nvme/command.hpp"
#include "nvme/controller_state.hpp"
#include "nvme/queue_entry.hpp"
#include "queue_pair.hpp"
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
#include <unordered_map>
#include <utility>
#include <vector>

namespace nvme {

using namespace std::chrono_literals;

struct NvmeDeviceHandle {
    NvmeDeviceHandle(os::MmioRegion bar0, dma::DmaHeap& dma) : bar0{bar0}, dma{dma} {
    }

    os::MmioRegion bar_region() const {
        return bar0;
    }

    dma::DmaHeap& dma_heap() const {
        return dma;
    }

private:
    os::MmioRegion bar0;
    dma::DmaHeap& dma;
};

struct QueueContext {
    QueueContext(QueueId qid, dma::DmaHeap& heap, ControllerRegisters& cregs, std::size_t depth)
        : queues{qid, heap, cregs, depth},
          cids{static_cast<Cid::value_type>(std::min<std::size_t>(depth - 1, 0xFFFE))} {
    }

    ~QueueContext() = default;

    QueueContext(const QueueContext&) = delete;
    QueueContext& operator=(const QueueContext&) = delete;
    QueueContext(QueueContext&&) = delete;
    QueueContext& operator=(QueueContext&&) = delete;

    QueuePair queues;
    CidAllocator cids;
    std::unordered_map<Cid, InFlightCommand> in_flight;
};

class Controller final {
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
        return submit_to(admin_queue_, std::move(command));
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
        QueueContext* ctx = io_queue_context(qid);
        if (ctx == nullptr) {
            return std::unexpected{Error{
                .reason = std::format("IO queue {} does not exist on this controller", qid.value),
                .severity = Error::Severity::ERROR
            }};
        }

        return submit_to(*ctx, std::move(command));
    }

    const QueuePair& admin_queues() const;
    const QueuePair* io_queue(QueueId qid) const;

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
    Result<CommandPending<Command>> submit_to(QueueContext& ctx, Command command) {
        if (ctx.queues.sq().full()) {
            return std::unexpected{Error{
                .reason = std::format("Submission queue {} is full", ctx.queues.id().value),
                .severity = Error::Severity::ERROR
            }};
        }

        Cid cmd_id = ctx.cids.allocate();

        wire::SubmissionEntry cmd = command.build(cmd_id.value);

        auto [it, inserted] = ctx.in_flight.try_emplace(
            cmd_id,
            InFlightCommand{.cid = cmd_id, .result = std::nullopt, .cmd = cmd}
        );

        if (!inserted) {
            // we have a problem: we submit a command but now we got it again?
            return std::unexpected{Error{
                .reason = "Encountered an already submitted CID",
                .severity = Error::Severity::ERROR
            }};
        }

        if (!ctx.queues.sq().push(cmd)) {  // copies the command into the submission queue's ring
            ctx.in_flight.erase(it);
            ctx.cids.release(cmd_id);
            return std::unexpected{Error{
                .reason =
                    std::format("Submission queue {} rejected the command", ctx.queues.id().value),
                .severity = Error::Severity::ERROR
            }};
        }

        return CommandPending<Command>{*this, ctx.queues.id(), cmd_id};
    }

    Result<QueueId> allocate_io_qid();

    void release_io_qid(QueueId qid);
 
    QueueContext* queue_context(QueueId qid);
    QueueContext* io_queue_context(QueueId qid);

    // blockingly polls the CQ until the completion for cid arrives or the timeout expires
    Result<wire::CompletionEntry> poll_command(QueueId qid, Cid cid);

    // non-blockingly drains whatever completions are visible on the cq
    std::optional<Result<wire::CompletionEntry>> try_poll_command(QueueId qid, Cid cid);

    void abandon_command(QueueId qid, Cid cid);

    // mark completion's cid as completed and write its result to in flight command map
    std::optional<Error>
    accept_completion(QueueContext& ctx, const wire::CompletionEntry& completion);

    // return the completion queue entry for a given cid and release that cid
    std::optional<wire::CompletionEntry> take_completion(QueueContext& ctx, Cid cid);

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

    QueueContext admin_queue_;
    std::vector<std::unique_ptr<QueueContext>> io_queues_;

    std::chrono::milliseconds timeout_;

    std::optional<ControllerState> state_;

    // qid management state
    std::uint16_t io_qid_grant_{0};
    std::uint16_t next_io_qid_{1};
    std::vector<std::uint16_t> free_io_qids_;
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
