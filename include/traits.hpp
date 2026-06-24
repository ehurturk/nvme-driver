#pragma once

#include <cstdint>

#include "nvme_spec.hpp"

namespace nvme
{
    // Tags for traits
    struct AdminQueueKind {};
    struct IOQueueKind {};

    // fwd declarations
    // implemented in admin_queue.hpp
    struct AdminSubmissionEntry;
    struct AdminCompletionEntry;

    // implemented in io_queue.hpp
    struct IOSubmissionEntry;
    struct IOCompletionEntry;

    struct QueueHardwareContext {
        std::uint16_t queue_id;
        volatile std::uint32_t* sq_tail_dbell;
        volatile std::uint32_t* cq_head_dbell;
    };

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
