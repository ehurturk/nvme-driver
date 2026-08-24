#pragma once

#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cstdint>

namespace nvme::admin::features {

enum class FeatureId : std::uint8_t {
    NumberOfQueues = 0x07,
    InterruptCoalescing = 0x08,
};

enum class Select : std::uint8_t {
    Current = 0b000,
    Default = 0b001,
    Saved = 0b010,
    SupportedCapability = 0b011,
};

template<typename F>
concept Feature = requires(const F f, const wire::CompletionEntry& e) {
    typename F::Completion;
    { F::id } -> std::convertible_to<FeatureId>;
    { f.value() } noexcept -> std::same_as<DWRD>;
    F::decode_completion(e);
};

// Number of Queues: Function Id: 0x07
struct NumberOfQueues {
    static constexpr FeatureId id = FeatureId::NumberOfQueues;

    // Write these as 1s based, it is converted to 0s based on encoding.
    std::uint16_t nsq{};  // number of submission queues
    std::uint16_t ncq{};  // number of completion queues

    DWRD value() const noexcept;

    // completion dw0
    struct Completion {
        std::uint16_t allocated_sq;
        std::uint16_t allocated_cq;
    };

    static Completion decode_completion(const wire::CompletionEntry& c) noexcept;

private:
    using NSQR =
        utils::BitField<DWRD, 0, 16>;  // 0 based, # of IOSQ requested (not including admin q)
    using NCQR =
        utils::BitField<DWRD, 16, 16>;  // 0 based, # of IOCQ requested (not including admin q)
};

// Interrupt Coalescing: Feature Id: 0x08
struct InterruptCoalescing {
    static constexpr FeatureId id = FeatureId::InterruptCoalescing;

    std::uint8_t threshold{};
    std::uint8_t time_100us{};

    DWRD value() const noexcept;

    struct Completion {};

    static Completion decode_completion(const wire::CompletionEntry&) noexcept;

private:
    using THR = utils::BitField<DWRD, 0, 8>;
    using TIME = utils::BitField<DWRD, 8, 8>;
};

static_assert(Feature<NumberOfQueues>);
static_assert(Feature<InterruptCoalescing>);

}  // namespace nvme::admin::features
