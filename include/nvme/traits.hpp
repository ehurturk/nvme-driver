#pragma once

#include "nvme/spec.hpp"

#include <cstddef>

namespace nvme {
// Tags for traits
struct AdminQueueKind {};

struct IOQueueKind {};

struct CommonCommand;
struct CompletionQueueEntry;

template<class Kind>
struct QueueTraits;

template<>
struct QueueTraits<AdminQueueKind> {
    using SubmissionEntry = CommonCommand;
    using CompletionEntry = ::nvme::CompletionQueueEntry;
    static constexpr std::size_t depth = constants::AQ_DEPTH;
};

template<>
struct QueueTraits<IOQueueKind> {
    using SubmissionEntry = CommonCommand;
    using CompletionEntry = ::nvme::CompletionQueueEntry;
    static constexpr std::size_t depth = constants::IO_DEPTH;
};
}  // namespace nvme
