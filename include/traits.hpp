#pragma once

#include "nvme_spec.hpp"

namespace nvme
{
    struct AdminQueue;
    struct IOQueue;

    struct AdminQueueKind {};

    struct IOQueueKind {};

    struct AdminSubmissionEntry;
    struct AdminCompletionEntry;

    struct IOSubmissionEntry;
    struct IOCompletionEntry;

    template <class Kind> struct QueueTraits;

    template <> struct QueueTraits<AdminQueueKind> {
        using SubmissionEntry = AdminSubmissionEntry;
        using CompletionEntry = AdminCompletionEntry;
        static constexpr std::size_t depth = constants::AQ_DEPTH;
    };

    template <> struct QueueTraits<IOQueueKind> {
        using SubmissionEntry = IOSubmissionEntry;
        using CompletionEntry = IOCompletionEntry;
        static constexpr std::size_t depth = constants::IO_DEPTH;
    };
}
