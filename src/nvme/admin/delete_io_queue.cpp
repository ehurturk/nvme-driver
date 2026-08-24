#include "nvme/admin/delete_io_queue.hpp"

namespace nvme::admin::commands {

CommonCommand DeleteIoCq::build(std::uint16_t this_cid) const noexcept {
    CommonCommand c = CommonCommand::build(Opcode::DeleteIoCq, this_cid);
    assert(qid != 0 && "Admin CQ can't be deleted.");
    c.cdw10 = utils::DwordBuilder<>{}.set<QID>(qid).get();
    return c;
}

DeleteIoCq::Completion DeleteIoCq::decode_completion(const CompletionQueueEntry&) {
    return Completion{};
}

CommonCommand DeleteIoSq::build(std::uint16_t this_cid) const noexcept {
    CommonCommand c = CommonCommand::build(Opcode::DeleteIoSq, this_cid);
    assert(qid != 0 && "Admin CQ can't be deleted.");
    c.cdw10 = utils::DwordBuilder<>{}.set<QID>(qid).get();
    return c;
}

DeleteIoSq::Completion DeleteIoSq::decode_completion(const CompletionQueueEntry&) {
    return Completion{};
}

}  // namespace nvme::admin::commands
