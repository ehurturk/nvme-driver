#pragma once

#include "../dma/heap.hpp"
#include "../os/mmio.hpp"
#include "../utils.hpp"
#include "cid.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "queue_pair.hpp"
#include "registers.hpp"
#include "spec.hpp"

#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace nvme {

using namespace std::chrono_literals;

struct NvmeDeviceHandle {
    os::MmioRegion bar0;
    dma::DmaHeap& dma;
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

    // pushes the command into the admin SQ and returns a CommandPending handle tied
    // to this command's type; dropping the handle abandons the command
    template<CommandDescriptor Command>
    Result<CommandPending<Command>> submit_admin_command(Command command) {
        Cid cmd_id = admin_cid_allocator_.allocate();
        auto cid_val = cmd_id.get();

        wire::SubmissionEntry cmd = command.build(cid_val);

        auto& asq = admin_queue_pair_.sq();
        asq.push(cmd);  // copies the command into the submission queue's ring queue

        auto [it, inserted] = in_flight_cmds_.try_emplace(
            cid_val,
            InFlightCommand{.cid = std::move(cmd_id), .result = std::nullopt, .cmd = cmd}
        );

        if (!inserted) {
            // we have a problem: we submit a command but now we got it again?
            return std::unexpected{Error{
                .reason = "Encountered an already submitted CID",
                .severity = Error::Severity::ERROR
            }};
        }

        return CommandPending<Command>{*this, cid_val};
    }

    AdminQueuePair& admin_queues();

    std::uint32_t max_io_queue_entries() const;

    bool requires_contiguous_queues() const;

private:
    template<CommandDescriptor Command>
    friend class CommandPending;

    // blockingly polls the admin CQ until the completion for cid arrives or the timeout expires
    Result<wire::CompletionEntry> poll_admin_command(Cid::value_type cid);

    // non-blockingly drains whatever completions are visible on the admin cq
    std::optional<Result<wire::CompletionEntry>> try_poll_admin_command(Cid::value_type cid);

    void abandon_admin_command(Cid::value_type cid);

    // mark completion's cid as completed and write its result to in flight command map
    std::optional<Error> accept_completion(const wire::CompletionEntry& completion);

    // return the completion queue entry for a given cid and release that cid
    std::optional<wire::CompletionEntry> take_completion(Cid::value_type cid);

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

private:
    ControllerRegisters regs_;
    AdminQueuePair admin_queue_pair_;
    std::chrono::milliseconds timeout_;

    CidAllocator admin_cid_allocator_;

    std::unordered_map<Cid::value_type, InFlightCommand> in_flight_cmds_;
};

template<CommandDescriptor Command>
CommandPending<Command>::CommandPending(CommandPending&& other) noexcept
    : controller_{std::exchange(other.controller_, nullptr)},
      cid_{other.cid_},
      completion_{other.completion_} {
}

template<CommandDescriptor Command>
CommandPending<Command>& CommandPending<Command>::operator=(CommandPending&& other) noexcept {
    if (this != &other) {
        release();
        controller_ = std::exchange(other.controller_, nullptr);
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
Cid::value_type CommandPending<Command>::cid() const {
    return cid_;
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

    auto polled = controller_->try_poll_admin_command(cid_);
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

    auto waited = controller_->poll_admin_command(cid_);
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
CommandPending<Command>::CommandPending(Controller& controller, Cid::value_type cid)
    : controller_{&controller}, cid_{cid} {
}

template<CommandDescriptor Command>
void CommandPending<Command>::release() {
    if (controller_ != nullptr && !completion_.has_value()) {
        controller_->abandon_admin_command(cid_);
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
