#pragma once

#include <cstdint>

#include "nvme_spec.hpp"

namespace nvme
{
// Tags for traits
struct AdminQueueKind
{};
struct IOQueueKind
{};

// fwd declarations
struct CommonCommand;   // implemented in queue_entry.hpp
struct CompletionEntry; // implemented in queue_entry.hpp

struct QueueHardwareContext
{
    std::uint16_t           queue_id;
    volatile std::uint32_t* sq_tail_dbell;
    volatile std::uint32_t* cq_head_dbell;
};

template <class Kind> struct QueueTraits;

template <> struct QueueTraits<AdminQueueKind>
{
    using SubmissionEntry              = CommonCommand;
    using CompletionEntry              = CompletionEntry;
    static constexpr std::size_t depth = constants::AQ_DEPTH;
};

template <> struct QueueTraits<IOQueueKind>
{
    using SubmissionEntry              = CommonCommand;
    using CompletionEntry              = CompletionEntry;
    static constexpr std::size_t depth = constants::IO_DEPTH;
};
} // namespace nvme
