#pragma once

#include <cstddef>

#include "nvme/spec.hpp"

namespace nvme
{
// Tags for traits
struct AdminQueueKind
{};
struct IOQueueKind
{};

struct CommonCommand;
struct CompletionEntry;

template <class Kind> struct QueueTraits;

template <> struct QueueTraits<AdminQueueKind>
{
    using SubmissionEntry              = CommonCommand;
    using CompletionEntry              = ::nvme::CompletionEntry;
    static constexpr std::size_t depth = constants::AQ_DEPTH;
};

template <> struct QueueTraits<IOQueueKind>
{
    using SubmissionEntry              = CommonCommand;
    using CompletionEntry              = ::nvme::CompletionEntry;
    static constexpr std::size_t depth = constants::IO_DEPTH;
};
} // namespace nvme
