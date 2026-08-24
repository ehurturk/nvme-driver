#pragma once

#include "nvme/spec.hpp"

#include <cstddef>

namespace nvme {
// Tags for traits
struct AdminQueueKind {};

struct IOQueueKind {};

namespace wire {
struct SubmissionEntry;
struct CompletionEntry;
}  // namespace wire

template<class Kind>
struct QueueTraits;

template<>
struct QueueTraits<AdminQueueKind> {
    using SubmissionEntry = wire::SubmissionEntry;
    using CompletionEntry = ::nvme::wire::CompletionEntry;
    static constexpr std::size_t depth = constants::AQ_DEPTH;
};

template<>
struct QueueTraits<IOQueueKind> {
    using SubmissionEntry = wire::SubmissionEntry;
    using CompletionEntry = ::nvme::wire::CompletionEntry;
    static constexpr std::size_t depth = constants::IO_DEPTH;
};
}  // namespace nvme
