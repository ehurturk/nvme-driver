#pragma once

#include <assert.h>

#include "nvme/queue_entry.hpp"
#include "utils.hpp"

namespace nvme
{
namespace features
{

// Bit fields:
using FID = utils::BitField<DWRD, 0, 8>;
using SEL = utils::BitField<DWRD, 8, 3>;
using SV  = utils::BitField<DWRD, 31, 1>;

// CDW14:
using UIDX = utils::BitField<DWRD, 0, 7>;

enum class FeatureId : std::uint8_t
{
    NumberOfQueues      = 0x07,
    InterruptCoalescing = 0x08,
};

enum class Select : std::uint8_t
{
    Current             = 0b000,
    Default             = 0b001,
    Saved               = 0b010,
    SupportedCapability = 0b011,
};

inline DWRD set_features_cdw10(FeatureId fid, bool save) noexcept
{
    return utils::DwordBuilder<>{}.set<FID>(utils::to_underlying(fid)).set<SV>(save).get();
}

inline DWRD get_features_cdw10(FeatureId fid, Select sel) noexcept
{
    return utils::DwordBuilder<>{}.set<FID>(utils::to_underlying(fid)).set<SEL>(utils::to_underlying(sel)).get();
}

inline DWRD cdw14_uuid_index(std::uint8_t uuid_index) noexcept
{
    return utils::DwordBuilder<>{}.set<UIDX>(uuid_index).get();
}

// Number of Queues: Function Id: 0x07
struct NumberOfQueues
{
    static constexpr FeatureId id = FeatureId::NumberOfQueues;

    // number of submission & completion queues.
    // Write these as 1s based, it is converted to 0s based on encoding.
    std::uint16_t nsq{};
    std::uint16_t ncq{};

    struct Cdw11
    {
        using NSQR = utils::BitField<DWRD, 0, 16>;  // 0 based
        using NCQR = utils::BitField<DWRD, 16, 16>; // 0 based
    };

    DWRD encode_cdw11() const noexcept
    {
        assert(nsq > 0);
        assert(ncq > 0);

        return utils::DwordBuilder<>{}.set<Cdw11::NSQR>(nsq - 1).set<Cdw11::NCQR>(ncq - 1).get();
    }

    // completion dw0
    struct Completion
    {
        std::uint16_t allocated_sq;
        std::uint16_t allocated_cq;
    };

    static Completion decode_completion(const CompletionEntry& c) noexcept
    {
        using NSQA = utils::BitField<DWRD, 0, 16>;
        using NCQA = utils::BitField<DWRD, 16, 16>;

        return Completion{
                .allocated_sq = static_cast<std::uint16_t>(NSQA::extract(c.completion_result.cdw0) + 1),
                .allocated_cq = static_cast<std::uint16_t>(NCQA::extract(c.completion_result.cdw0) + 1),
        };
    }
};

// Interrupt Coalescing: Feature Id: 0x08
struct InterruptCoalescing
{
    static constexpr FeatureId id = FeatureId::InterruptCoalescing;

    std::uint8_t threshold{};
    std::uint8_t time_100us{};

    struct Cdw11
    {
        using THR  = utils::BitField<DWRD, 0, 8>;
        using TIME = utils::BitField<DWRD, 8, 8>;
    };

    DWRD encode_cdw11() const noexcept
    {
        return utils::DwordBuilder<>{}.set<Cdw11::THR>(threshold).set<Cdw11::TIME>(time_100us).get();
    }

    struct Completion
    {};

    static Completion decode_completion(const CompletionEntry& c) noexcept
    {
        return Completion{};
    }
};
} // namespace features

} // namespace nvme
