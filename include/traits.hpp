#pragma once

#include "nvme_spec.hpp"

namespace nvme
{
    struct AdminQueue;
    struct IOQueue;

    struct AdminSubmissionEntry;
    struct AdminCompletionEntry;

    struct IOSubmissionEntry;
    struct IOCompletionEntry;

    template <class Derived> struct QueueTraits;

    template <> struct QueueTraits<AdminQueue> {
        using SubmissionEntry = AdminSubmissionEntry;
        using CompletionEntry = AdminCompletionEntry;
        static constexpr std::size_t value = constants::AQ_DEPTH;
    };

    template <> struct QueueTraits<IOQueue> {
        using SubmissionEntry = IOSubmissionEntry;
        using CompletionEntry = IOCompletionEntry;
        static constexpr std::size_t value = constants::IO_DEPTH;
    };
}
