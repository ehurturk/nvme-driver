#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <optional>

#include "dma/buffer.hpp"
#include "lifetime.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/traits.hpp"
#include "utils.hpp"

namespace nvme
{
template <class Kind, std::size_t N = QueueTraits<Kind>::depth> class CompletionQueue
{
  public:
    using Traits = QueueTraits<Kind>;
    using Entry  = CompletionEntry;

    static constexpr std::size_t depth = N;

    static_assert(std::has_single_bit(depth), "Queue size must be a power of 2.");
    static_assert(sizeof(Entry) == 16, "Completion queue entry size must be 16 bytes.");

    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);
    static_assert(depth > 1);
    static_assert(depth <= 65536);

    CompletionQueue(dma::DmaView memory, volatile std::uint32_t* head_doorbell)
            : memory_{memory}, doorbell_{head_doorbell}, expected_bit_{1}
    {
        assert(memory_.size >= depth * sizeof(Entry));
    }

    // non blocking poll
    std::optional<Entry> poll()
    {
        //              H   T
        //  SQ:   | x | x | x |   |   |   |   |   |   |   |   |   |
        //  CQ:   | 1 | 1 |   |   |   |   |   |   |   |   |   |   |
        //                  H
        //        Expecting phase: 1

        // 1) Peek at the CQE at head slot
        std::byte* dst = memory_.uva + head_ * sizeof(Entry);

        assert(reinterpret_cast<std::uintptr_t>(dst) % alignof(Entry) == 0);

        Entry* e = compat::load_object_from_bytes<Entry>(dst);

        std::uint16_t raw_status = utils::ReadVolatile(e->status);
        CompletionStatus status { .status = raw_status };

        if (status.phase() != expected_bit_) {
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

    std::size_t head() const
    {
        return head_;
    }

  private:
    void ring_head_doorbell()
    {
        *doorbell_ = static_cast<std::uint32_t>(head_);
    }

  private:
    dma::DmaView            memory_;
    volatile std::uint32_t* doorbell_;
    std::size_t             head_{};

    std::uint8_t expected_bit_;
};
} // namespace nvme
