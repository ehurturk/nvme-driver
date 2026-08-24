#pragma once

#include "nvme/cid.hpp"
#include "nvme/queue_entry.hpp"

#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace nvme {

class Controller;

template<typename C>
concept CommandDescriptor = std::default_initializable<C> &&
                            requires(const C c, std::uint16_t cid, const CompletionQueueEntry& e) {
                                { c.build(cid) } noexcept -> std::same_as<CommonCommand>;
                                typename C::Completion;
                                { C::decode_completion(e) } -> std::same_as<typename C::Completion>;
                            };

struct InFlightCommand {
    Cid cid;
    std::optional<CompletionQueueEntry> result;
    CommonCommand cmd;
    // set when the CommandPending for this command was dropped without consuming
    // the result: the entry (and its CID) is freed as soon as the completion arrives
    bool abandoned = false;
};

struct ControllerDiagnostic {
    enum class Severity {
        INFO,
        WARN,
        ERROR
    };

    std::string reason;
    Severity severity;
};

// a move-only, one-shot handle to an in-flight admin command, vended by
// Controller::submit_admin_command; the Command type parameter ties the
// completion back to the decoder of the command that produced it, so a
// completion can only ever be decoded as what was actually submitted.
// NOTE: must not outlive the Controller it came from
template<CommandDescriptor Command>
class CommandPending {
public:
    CommandPending(const CommandPending&) = delete;
    CommandPending& operator=(const CommandPending&) = delete;

    CommandPending(CommandPending&& other) noexcept;

    CommandPending& operator=(CommandPending&& other) noexcept;

    // dropping an unconsumed handle abandons the command: the controller frees the
    // CID slot once the (discarded) completion arrives
    ~CommandPending();

    Cid::value_type cid() const;

    // true once the completion has been consumed into this handle
    bool done() const;

    // non-blocking: nullopt means the command is still in flight
    std::expected<std::optional<CompletionQueueEntry>, ControllerDiagnostic> poll();

    // blocking: waits until the completion arrives or the admin timeout expires;
    // idempotent once the completion has been consumed
    std::expected<CompletionQueueEntry, ControllerDiagnostic> wait();

    // blocking: waits, requires a success status, and decodes the completion with
    // this command's own decoder; use wait() to inspect a raw (possibly failed) CQE
    std::expected<typename Command::Completion, ControllerDiagnostic> result();

private:
    friend class Controller;

    CommandPending(Controller& controller, Cid::value_type cid);

    void release();

    ControllerDiagnostic empty_handle_diagnostic() const;

    Controller* controller_ = nullptr;
    Cid::value_type cid_{};
    std::optional<CompletionQueueEntry> completion_;
};

}  // namespace nvme
