#pragma once

#include "nvme/admin/features.hpp"
#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cstdint>

namespace nvme::admin::commands {

// SetFeatures (0x09)
// CDW10: bits 0 - 7: Feature Identifier (FID)
//               31 : Save (SV): specifies that the controller shall save the attribute so that the
//               attribute persists
//                               through all power states and resets.
// For feature NumberOfQueues (0x07):
//   CDW11: bits 10 - 15: Number of I/O Sub Queues Requested (NSQR): # of IO queues requested (not
//   including the admin queue).
//                                                                   is 0s based
//               16 - 31: Number of I/O Compl. Queues Requested (NCQR), is 0s based
// For feature InterruptCoalescing (0x08):
//   CDW11: bits 0 - 7: Aggregation Threshold (THR): Specifies the recommended minimum number of
//   completion queue
//                                                   entries to aggregate per interrupt vector
//                                                   before signaling an interrupt to the host. Is a
//                                                   0's based value
//          bits 8 - 15: Aggregation Time (TIME): Specifies the recommended maximum time in 100
//          microsecond increments
//                                                that a controller may delay an interrupt due to
//                                                interrupt coalescing.
// CDW14: bits 0 - 6: UUID index
template<features::Feature F>
struct SetFeatures {
    explicit SetFeatures(F feature, bool save = false, std::uint8_t uuid_index = 0)
        : feature{std::move(feature)}, save{save}, uuid_index{uuid_index} {
    }

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept {
        wire::SubmissionEntry c = build_base(Opcode::SetFeatures, this_cid);

        c.dptr = wire::DataPointer::prp(prp1);
        c.cdw10 = utils::DwordBuilder<>{}
                      .set<FID>(utils::to_underlying(F::id))
                      .template set<SV>(save)
                      .get();
        c.cdw11 = feature.value();
        c.cdw14 = utils::DwordBuilder<>{}.set<UIDX>(uuid_index).get();

        return c;
    }

    using Completion = typename F::Completion;

    static Completion decode_completion(const wire::CompletionEntry& e) {
        return F::decode_completion(e);
    }

private:
    using FID = utils::BitField<DWRD, 0, 8>;
    using SV = utils::BitField<DWRD, 31, 1>;
    using UIDX = utils::BitField<DWRD, 0, 7>;

    F feature;
    bool save;
    std::uint64_t prp1 = 0;  // no supported feature carries a data buffer yet
    std::uint8_t uuid_index;
};

template<features::Feature F>
struct GetFeatures {
    explicit GetFeatures(
        features::Select sel = features::Select::Current,
        std::uint8_t uuid_index = 0
    )
        : sel{sel}, uuid_index{uuid_index} {
    }

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept {
        wire::SubmissionEntry c = build_base(Opcode::GetFeatures, this_cid);

        c.dptr = wire::DataPointer::prp(prp1);
        c.cdw10 = utils::DwordBuilder<>{}
                      .set<FID>(utils::to_underlying(F::id))
                      .template set<SEL>(utils::to_underlying(sel))
                      .get();
        c.cdw14 = utils::DwordBuilder<>{}.set<UIDX>(uuid_index).get();

        return c;
    }

    using Completion = typename F::Completion;

    static Completion decode_completion(const wire::CompletionEntry& e) {
        return F::decode_completion(e);
    }

private:
    using FID = utils::BitField<DWRD, 0, 8>;
    using SEL = utils::BitField<DWRD, 8, 3>;
    using UIDX = utils::BitField<DWRD, 0, 7>;

    features::Select sel;
    std::uint64_t prp1 = 0;
    std::uint8_t uuid_index;
};

static_assert(CommandDescriptor<SetFeatures<features::NumberOfQueues>>);
static_assert(CommandDescriptor<GetFeatures<features::NumberOfQueues>>);

}  // namespace nvme::admin::commands
