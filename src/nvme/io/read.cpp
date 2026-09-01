#include "nvme/io/read.hpp"
#include "nvme/queue_entry.hpp"

namespace nvme::io::commands {

wire::SubmissionEntry Read::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry entry = build_base(Opcode::Read, this_cid);
    return entry;
}

Read::Completion Read::decode_completion(const wire::CompletionEntry& c) {
    Read::Completion comp;
    std::uint8_t status_val = c.status.status_code();
    
    if (status_val == Read::Completion::CONFLICTING_ATTRIBUTES_STATUS_VAL) {
        comp.conflicting_attributes = true;
    }

    if (status_val == Read::Completion::INVALID_PROT_INFO_STATUS_VAL) {
        comp.invalid_prot_information = true;
    }

    return comp;
}

} // namespace nvme::io::commands