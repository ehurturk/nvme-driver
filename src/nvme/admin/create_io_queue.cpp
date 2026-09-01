#include "nvme/admin/create_io_queue.hpp"

namespace nvme::admin::commands {

wire::SubmissionEntry CreateIoCq::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry c = wire::SubmissionEntry::build(Opcode::CreateIoCq, this_cid);

    c.dptr = wire::DataPointer::prp(prp1);
    c.cdw10 = utils::DwordBuilder<>{}.set<QID>(qid).set<QSIZE>(utils::to_zero_based(qsize)).get();
    c.cdw11 = utils::DwordBuilder<>{}
                  .set<PC>(static_cast<PC::value_type>(physically_contiguous))
                  .set<IEN>(static_cast<IEN::value_type>(interrupts_enabled))
                  .set<IV>(interrupt_vector)
                  .get();

    return c;
}

CreateIoCq::Completion CreateIoCq::decode_completion(const wire::CompletionEntry& c) {
    return Completion{.status = c.status.status_code()};
}

wire::SubmissionEntry CreateIoSq::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry c = wire::SubmissionEntry::build(Opcode::CreateIoSq, this_cid);

    c.dptr = wire::DataPointer::prp(prp1);
    c.cdw10 = utils::DwordBuilder<>{}.set<QID>(qid).set<QSIZE>(utils::to_zero_based(qsize)).get();
    c.cdw11 = utils::DwordBuilder<>{}
                  .set<PC>(static_cast<PC::value_type>(physically_contiguous))
                  .set<QPRIO>(utils::to_underlying(priority))
                  .set<CQID>(cqid)
                  .get();
    c.cdw12 = utils::DwordBuilder<>{}.set<NVMSETID>(nvmsetid).get();

    return c;
}

CreateIoSq::Completion CreateIoSq::decode_completion(const wire::CompletionEntry&) {
    return Completion{};
}

}  // namespace nvme::admin::commands
