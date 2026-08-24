#include "nvme/admin/abort.hpp"

namespace nvme::admin::commands {

wire::SubmissionEntry Abort::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry c = wire::SubmissionEntry::build(Opcode::Abort, this_cid);
    c.cdw10 = utils::DwordBuilder<>{}.set<SQID>(sqid).set<CID>(cid).get();
    return c;
}

Abort::Completion Abort::decode_completion(const wire::CompletionEntry& c) {
    return Completion{.immediate_abort_not_performed = (c.completion_result.cdw0 & 0x1) == 1};
}

}  // namespace nvme::admin::commands
