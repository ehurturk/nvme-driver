#pragma once

#include <array>
#include <vector>
#include <cstdint>

#include "traits.hpp"

namespace nvme
{

using IOVA = std::uint64_t;

struct DMAMemory {
    std::byte *uva; // userspace virtual addr
    IOVA iova;      // device virtual addr
    std::size_t size;
};

// CRTP-based dispatch on for Admin/IO
// queues. This is a fixed-size ring queue
// with size specified as N.
template <class Derived,
        class Entry = typename QueueTraits<Derived>::SubmissionEntry,
        std::size_t N = QueueTraits<Derived>::value>
class Queue
{
  public:
    using entry_type = Entry;
    static constexpr std::size_t depth = N;
    DMAMemory mem_;

    void submit()
    {
        static_cast<Derived *>(this)->submit_impl();
    }

    void push()
    {
    }

    void pop()
    {
    }

    template <typename... Args> void emplace(Args &&...args)
    {
    }

    inline std::size_t size() const
    {
        return size_;
    }

  private:
    alignas(Entry) std::array<std::byte, sizeof(Entry) * depth> buf_{};

    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
};

// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
template <class T> struct QueuePair {
    std::vector<Queue<T, typename QueueTraits<T>::SubmissionEntry>> submission_queues_;
    Queue<T, typename QueueTraits<T>::CompletionEntry> completion_queue_;
};

} // namespace nvme
