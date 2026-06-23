#pragma once

#include <utility>
#include <atomic>
#include <bit>

#include "traits.hpp"
#include "dma_ring.hpp"

namespace nvme
{

template <class Kind, std::size_t N = QueueTraits<Kind>::depth>
class SubmissionQueue
{
  public:
    using Traits = QueueTraits<Kind>;
    using Entry = typename Traits::SubmissionEntry;

    static constexpr std::size_t depth = N;

    static_assert(
            std::has_single_bit(depth), "Queue size must be a power of 2.");
    static_assert(sizeof(Entry) == 64, "Entry size must be 64 bytes.");

    explicit SubmissionQueue(DmaMemoryDescriptor dma_region)
            : dma_region_{std::move(dma_region)}
    {
    }

    bool write(const Entry &e)
    {
        if (full())
            return false;

        slots()[tail_] = e;

        std::atomic_thread_fence(std::memory_order_release);

        tail_ = (tail_ + 1) & (depth - 1);

        // write to mmapped BAR0 doorbell register
        ring_tail_doorbell();

        return true;
    }

    // should be called when a completion is done in the
    // completion queue.
    void advance_head(std::size_t head)
    {
        head_ = head;
    }

    bool full() const
    {
        return ((tail_ + 1) & (depth - 1)) == head_;
    }

    Entry *slots()
    {
        return std::launder(reinterpret_cast<Entry *>(dma_region_.uva()));
    }

    void ring_tail_doorbell()
    {
    }

  private:
    DmaMemoryDescriptor dma_region_;
    std::size_t tail_{};
    std::size_t head_{};
};

} // namespace nvme
