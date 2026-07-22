#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <cstring>
#include <print>

#include "dma/buffer.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/traits.hpp"

namespace nvme
{

template <class Kind, std::size_t N = QueueTraits<Kind>::depth> class SubmissionQueue
{
  public:
    using Traits = QueueTraits<Kind>;
    using Entry  = typename Traits::SubmissionEntry;

    static constexpr std::size_t depth = N;

    static_assert(std::has_single_bit(depth), "Queue size must be a power of 2.");
    static_assert(sizeof(Entry) == 64, "Entry size must be 64 bytes.");

    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);
    static_assert(depth > 1);
    static_assert(depth <= 65536);

    SubmissionQueue(dma::DmaView dma_region, volatile std::uint32_t* tail_doorbell)
            : dma_region_{dma_region}, doorbell_{tail_doorbell}
    {
        assert(dma_region_.size >= depth * sizeof(Entry));
    }

    // writes + commits a single entry to the queue
    bool push(const Entry& e)
    {
        if (!write(e))
            return false;

        commit();
        return true;
    }

    // writes but does not ring the doorbell
    bool write(const Entry& e)
    {
        if (full())
            return false;

        // copy entry
        auto* dst = dma_region_.uva + tail_ * sizeof(Entry);
        std::memcpy(dst, &e, sizeof(Entry));

        tail_ = (tail_ + 1) & (depth - 1);
        return true;
    }

    // rings the doorbell
    void commit()
    {
        // ensure writes are done before the doorbell update
        std::atomic_thread_fence(std::memory_order_release);

        ring_tail_doorbell();
    }

    // NOTE: should be called when a completion is done in the
    // completion queue
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

    std::size_t head() const { return head_; }
    std::size_t tail() const { return tail_; }

  private:
    void ring_tail_doorbell()
    {
        *doorbell_ = static_cast<std::uint32_t>(tail_);
    }

  private:
    dma::DmaView            dma_region_;
    volatile std::uint32_t* doorbell_;
    std::size_t             tail_{};
    std::size_t             head_{};
};

} // namespace nvme
