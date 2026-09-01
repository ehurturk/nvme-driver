#include "nvme/queue_entry.hpp"

#include "utils.hpp"

#include <cstdint>

namespace nvme::wire {

std::uint8_t SubmissionEntry::opcode() const {
    return static_cast<std::uint8_t>(OPC::extract(cdw0));
}

std::uint16_t SubmissionEntry::cid() const {
    return static_cast<std::uint16_t>(CID::extract(cdw0));
}

std::uint8_t SubmissionEntry::psdt() const {
    return static_cast<std::uint8_t>(PSDT::extract(cdw0));
}

std::uint8_t SubmissionEntry::fuse() const {
    return static_cast<std::uint8_t>(FUSE::extract(cdw0));
}

bool CompletionStatus::succeeded() const {
    // SC bits are bits 1-8 and SCT Bits are bits 9-11
    // so this bitmask is bits 1-11:
    // 0x0ffe = 0000_1111_1111_1110
    return (status & 0x0ffe) == 0;
}

std::uint8_t CompletionStatus::status_code() const {
    return static_cast<std::uint8_t>(SC::extract(status));
}

CompletionStatus::StatusCodeType CompletionStatus::status_code_type() const {
    auto val = SCT::extract(status);
    return static_cast<StatusCodeType>(val);
}

std::uint8_t CompletionStatus::phase() const {
    return static_cast<std::uint8_t>(P::extract(status));
}

std::uint8_t CompletionStatus::command_retry_delay() const {
    return static_cast<std::uint8_t>(CRD::extract(status));
}

bool CompletionStatus::do_not_retry() const {
    return static_cast<bool>(DNR::extract(status));
}

bool CompletionStatus::more() const {
    return static_cast<bool>(M::extract(status));
}

CompletionStatus::operator int() const noexcept {
    return status;
}

bool CompletionEntry::ok() const {
    return status.succeeded();
}

}  // namespace nvme::wire
