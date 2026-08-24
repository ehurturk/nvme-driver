#include "nvme/admin/features.hpp"

namespace nvme::admin::features {

DWRD NumberOfQueues::value() const noexcept {
    return utils::DwordBuilder<>{}
        .set<NSQR>(utils::to_zero_based(nsq))
        .set<NCQR>(utils::to_zero_based(ncq))
        .get();
}

NumberOfQueues::Completion
NumberOfQueues::decode_completion(const CompletionQueueEntry& c) noexcept {
    using NSQA = utils::BitField<DWRD, 0, 16>;   // # of IOSQ allocated
    using NCQA = utils::BitField<DWRD, 16, 16>;  // # of IOCQ allocated

    return Completion{
        .allocated_sq = static_cast<std::uint16_t>(
            utils::to_ones_based(NSQA::extract(c.completion_result.cdw0))
        ),
        .allocated_cq = static_cast<std::uint16_t>(
            utils::to_ones_based(NCQA::extract(c.completion_result.cdw0))
        ),
    };
}

DWRD InterruptCoalescing::value() const noexcept {
    return utils::DwordBuilder<>{}.set<THR>(threshold).set<TIME>(time_100us).get();
}

InterruptCoalescing::Completion
InterruptCoalescing::decode_completion(const CompletionQueueEntry&) noexcept {
    return Completion{};
}

}  // namespace nvme::admin::features
