#pragma once

#include "nvme/admin/opcodes.hpp"
#include "nvme/cid.hpp"
#include "nvme/io/opcodes.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace nvme {

class Controller;

struct QueueId {
    std::uint16_t value;
};

struct CompletionQueueId {
    std::uint16_t value;
};

template<typename Opc>
concept Opcode = std::same_as<Opc, admin::Opcode> || std::same_as<Opc, io::Opcode>;

template<Opcode Opc>
wire::SubmissionEntry build_base(
    Opc opc,
    std::uint16_t cid,
    wire::SubmissionEntry::Psdt psdt = wire::SubmissionEntry::Psdt::Prp
) {
    using OPC = utils::BitField<DWRD, 0, 8>;
    using PSDT = utils::BitField<DWRD, 14, 2>;
    using CID = utils::BitField<DWRD, 16, 16>;

    wire::SubmissionEntry c{};
    c.cdw0 = utils::DwordBuilder<>{}
                 .set<OPC>(utils::to_underlying(opc))
                 .template set<PSDT>(utils::to_underlying(psdt))
                 .template set<CID>(cid)
                 .get();
    return c;
}

template<typename C>
concept CommandDescriptor = requires(const C c, std::uint16_t cid, const wire::CompletionEntry& e) {
    { c.build(cid) } noexcept -> std::same_as<wire::SubmissionEntry>;
    typename C::Completion;
    { C::decode_completion(e) } -> std::same_as<typename C::Completion>;
};

struct InFlightCommand {
    Cid cid{0};
    std::optional<wire::CompletionEntry> result{std::nullopt};
    wire::SubmissionEntry cmd{};

    bool abandoned = false;
};

struct Error {
    enum class Severity : std::uint8_t {
        INFO,
        WARN,
        ERROR
    };

    std::string reason;
    Severity severity;
};

template<typename T>
using Result = std::expected<T, Error>;

template<CommandDescriptor Command>
class CommandPending {
public:
    CommandPending(const CommandPending&) = delete;
    CommandPending& operator=(const CommandPending&) = delete;

    CommandPending(CommandPending&& other) noexcept;
    CommandPending& operator=(CommandPending&& other) noexcept;

    ~CommandPending();

    Cid cid() const;

    // true once the completion has been consumed into this handle
    bool done() const;

    // non-blocking: nullopt means the command is still in flight
    std::optional<Result<wire::CompletionEntry>> poll();

    // blocking: waits until the completion arrives or the admin timeout expires
    Result<wire::CompletionEntry> wait();

    // blocking: waits and decodes the completion
    Result<typename Command::Completion> result();

private:
    friend class Controller;

    CommandPending(Controller& controller, Cid cid);

    void release();

    Error empty_handle_diagnostic() const;

    Controller* controller_ = nullptr;
    Cid cid_{};
    std::optional<wire::CompletionEntry> completion_;
};

}  // namespace nvme
