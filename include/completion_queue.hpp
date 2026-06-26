#pragma once

#include <atomic>
#include <optional>
#include <cstring>

#include "dma_memory.hpp"
#include "traits.hpp"
#include "lifetime.hpp"
#include "queue_entry.hpp"

namespace nvme
{
template <class Kind, std::size_t N = QueueTraits<Kind>::depth>
class CompletionQueue
{
  public:
    using Trait = QueueTraits<Kind>;
    using Entry = CompletionEntry;

    static constexpr std::size_t depth = N;

    static_assert(
            std::has_single_bit(depth), "Queue size must be a power of 2.");
    static_assert(sizeof(Entry) == 16,
            "Completion queue entry size must be 16 bytes.");

    // Check trivially copyable and standard layout for cheap copies & semantics
    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);
    static_assert(depth > 1);
    static_assert(depth <= 65536);

    CompletionQueue(DmaMemory mem, QueueHardwareContext context)
            : memory_{std::move(mem)}, queue_id_{context.queue_id},
              doorbell_{context.cq_head_dbell}, expected_bit_{1}
    {
    }

    std::optional<Entry> poll()
    {
        //              H   T
        //  SQ:   | x | x | x |   |   |   |   |   |   |   |   |   |
        //  CQ:   | 1 | 1 |   |   |   |   |   |   |   |   |   |   |
        //                  H
        //        Expecting phase: 1

        // 1) Peek at the CQE at head slot
        std::byte *dst = memory_.uva() + head_ * sizeof(Entry);

        Entry *e = compat::load_object_from_bytes<Entry>(dst);

        std::uint16_t status = utils::ReadVolatile(e->status);
        std::uint8_t phase = cqe::phase(status);

        if (phase != expected_bit_) {
            return std::nullopt;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        Entry cqe = *e;

        // advance the head index
        head_ = (head_ + 1) & (depth - 1);

        // on each queue rollover, the phase bit should flip
        if (head_ == 0)
            expected_bit_ ^= 1;

        // notify the controller for a read of cqe
        ring_head_doorbell();

        return cqe;
    }

    std::size_t head() const { return head_; }

  private:
    void ring_head_doorbell()
    {
        *doorbell_ = head_;
    }

  private:
    DmaMemory memory_;
    std::uint16_t queue_id_;
    volatile std::uint32_t *doorbell_;
    std::size_t head_{};

    // the expected phase tag bit, initially starts as 1 for the very first CQE.
    std::uint8_t expected_bit_;
};
} // namespace nvme
