#include "nvme/admin/delete_io_queue.hpp"

namespace nvme::admin::commands {

wire::SubmissionEntry DeleteIoCq::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry c = wire::SubmissionEntry::build(Opcode::DeleteIoCq, this_cid);
    assert(qid != 0 && "Admin CQ can't be deleted.");
    c.cdw10 = utils::DwordBuilder<>{}.set<QID>(qid).get();
    return c;
}

DeleteIoCq::Completion DeleteIoCq::decode_completion(const wire::CompletionEntry&) {
    return Completion{};
}

wire::SubmissionEntry DeleteIoSq::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry c = wire::SubmissionEntry::build(Opcode::DeleteIoSq, this_cid);
    assert(qid != 0 && "Admin CQ can't be deleted.");
    c.cdw10 = utils::DwordBuilder<>{}.set<QID>(qid).get();
    return c;
}

DeleteIoSq::Completion DeleteIoSq::decode_completion(const wire::CompletionEntry&) {
    return Completion{};
}

}  // namespace nvme::admin::commands
