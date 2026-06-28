#pragma once

#include <atomic>
#include <bit>
#include <cstring>

#include "traits.hpp"
#include "dma_memory_view.hpp"

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

    // Check trivially copyable and standard layout for cheap copies & semantics
    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);
    static_assert(depth > 1);
    static_assert(depth <= 65536);

    SubmissionQueue(
            DmaMemoryView dma_region, QueueHardwareContext context)
            : dma_region_{dma_region}, queue_id_{context.queue_id},
              doorbell_{context.sq_tail_dbell}
    {
    }

    // Writes and commits a single Entry to the queue.
    bool push(const Entry &e)
    {
        if (full())
            return false;

        // copy the entry to the desired location
        auto *dst = dma_region_.uva + tail_ * sizeof(Entry);
        std::memcpy(dst, &e, sizeof(Entry));

        // ensure slot is written before tail update
        std::atomic_thread_fence(std::memory_order_release);

        tail_ = (tail_ + 1) & (depth - 1);

        // write to mmapped BAR0 doorbell register
        ring_tail_doorbell();

        return true;
    }

    // Writes an Entry to the queue without ringing the tail doorbell.
    bool write(const Entry &e)
    {
        if (full())
            return false;

        // copy the entry to the desired location
        auto *dst = dma_region_.uva + tail_ * sizeof(Entry);
        std::memcpy(dst, &e, sizeof(Entry));

        // ensure slot is written before tail update
        std::atomic_thread_fence(std::memory_order_release);

        tail_ = (tail_ + 1) & (depth - 1);
        return true;
    }

    // Commits a series of writes to the controller.
    void commit()
    {
        // TODO: maybe keep track of the previous
        // committed tail and then check whether it has
        // been advanced?
        ring_tail_doorbell();
    }

    // should be called when a completion is done in the
    // completion queue.
    void advance_head(std::size_t head)
    {
        if (head >= depth) {
            std::println("[ERROR]: Invalid NVMe SQ head.");
            return;
        }
        head_ = head;
    }

    bool full() const
    {
        return ((tail_ + 1) & (depth - 1)) == head_;
    }

  private:
    void ring_tail_doorbell()
    {
        *doorbell_ = tail_;
    }

  private:
    DmaMemoryView dma_region_;
    std::uint16_t queue_id_;
    volatile std::uint32_t *doorbell_;
    std::size_t tail_{};
    std::size_t head_{};
};

} // namespace nvme
