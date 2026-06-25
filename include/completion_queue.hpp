#pragma once

#include "dma_memory.hpp"
#include "traits.hpp"

namespace nvme
{
template <class Kind, std::size_t N = QueueTraits<Kind>::depth>
class CompletionQueue
{
  public:
    using Trait = QueueTraits<Kind>;
    using Entry = Trait::CompletionEntry;

    static constexpr std::size_t depth = N;

    static_assert(
            std::has_single_bit(depth), "Queue size must be a power of 2.");
    static_assert(sizeof(Entry) == 64, "Entry size must be 64 bytes.");

    // Check trivially copyable and standard layout for cheap copies & semantics
    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);
    static_assert(depth > 1);
    static_assert(depth <= 65536);

    CompletionQueue(DmaMemory mem, QueueHardwareContext context)
            : memory_{std::move(mem)}, queue_id_{context.queue_id},
              doorbell_{context.cq_head_dbell}
    {
    }

    Entry read()
    {
        // TODO: may return Entry& as well?
        // because the memory is there anyways and we are constructing an
        // entry object from the memory?
    }

  private:
    void ring_head_doorbell()
    {
        *doorbell_ = head_;
    }

  private:
    DmaMemory memory_;
    std::uint16_t queue_id_;
    volatile std::uint32_t *doorbell_;
    std::size_t tail_{};
    std::size_t head_{};

    std::uint8_t expected_bit_;
};
} // namespace nvme
