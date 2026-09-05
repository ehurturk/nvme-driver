#pragma once

#include <cstdint>

namespace nvme::io {

enum AccessLatency: std::uint8_t {
    Default = 0,
    Idle,
    Normal,
    Low
};

enum class AccessFrequency: std::uint8_t {
    Default = 0,
    TypicalRW,
    InfrequentRW,
    InfrequentW_FrequentR,
    FrequentW_InfrequentR,
    FrequentRW,
    OneTimeRead,
    SpeculativeRead,
    OverwrittenInFuture
};

struct DatasetManagement {
    bool incompressable;
    bool seq_request;
    AccessLatency latency;
    AccessFrequency frequency;
};

} // namespace nvme::io