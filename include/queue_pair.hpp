#pragma once

#include <vector>
#include <cstdint>

namespace nvme
{

struct IOVA {
    std::uint64_t value;
};

struct DMAMemory {
    std::byte *uva;
    IOVA iova;
    std::size_t size;
};

// CRTP-based dispatch on queues to Admin/IO
// queues.
template <class Derived> struct SubmissionQueue {
    void submit()
    {
        static_cast<Derived *>(this)->submit_impl();
    }

    DMAMemory mem_info_;
};

struct CompletionQueue {
    void submit()
    {
    }

    DMAMemory mem_info_;
};

// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
template <class QueueType> struct QueuePair {
    // TODO: Convert this to a SmallVector in the future
    // so queues are stack allocated
    std::vector<SubmissionQueue<QueueType>> submission_queues_;
    CompletionQueue completion_queue_;
};

} // namespace nvme
