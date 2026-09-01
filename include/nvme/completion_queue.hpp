#pragma once

#include "dma/buffer.hpp"
#include "lifetime.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/spec.hpp"
#include "utils.hpp"

#include <atomic>
#include <bit>
#include <cassert>
#include <optional>

namespace nvme {
class CompletionQueue {
public:
    using Entry = wire::CompletionEntry;

    static_assert(sizeof(Entry) == 16, "Completion queue entry size must be 16 bytes.");

    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);

    CompletionQueue(dma::DmaView memory, volatile std::uint32_t* head_doorbell, std::size_t depth)
        : memory_{memory}, doorbell_{head_doorbell}, depth_{depth}, expected_bit_{1} {
        assert(std::has_single_bit(depth_) && "Queue size must be a power of 2.");
        assert(depth_ > 1);
        assert(
            depth_ <= constants::MAX_IO_QUEUE_DEPTH
        );  // although admin queues are 4096 i think we can default the bound to IO Queue
        assert(memory_.size >= depth_ * sizeof(Entry));
    }

    // non blocking poll
    std::optional<Entry> poll() {
        //              H   T
        //  SQ:   | x | x | x |   |   |   |   |   |   |   |   |   |
        //  CQ:   | 1 | 1 |   |   |   |   |   |   |   |   |   |   |
        //                  H
        //        Expecting phase: 1

        // 1) Peek at the CQE at head slot
        std::byte* dst = memory_.uva + (head_ * sizeof(Entry));

        assert(reinterpret_cast<std::uintptr_t>(dst) % alignof(Entry) == 0);

        Entry* e = compat::load_object_from_bytes<Entry>(dst);

        std::uint16_t raw_status = utils::ReadVolatile(e->status.status);
        wire::CompletionStatus status{.status = raw_status};

        if (status.phase() != expected_bit_) {
            return std::nullopt;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        Entry cqe = *e;

        // advance the head index
        head_ = (head_ + 1) & (depth_ - 1);

        // on each queue rollover, the phase bit should flip
        if (head_ == 0) {
            expected_bit_ ^= 1;
        }

        // notify the controller for a read of cqe
        ring_head_doorbell();

        return cqe;
    }

    std::size_t depth() const {
        return depth_;
    }

    std::size_t head() const {
        return head_;
    }

private:
    void ring_head_doorbell() {
        *doorbell_ = static_cast<std::uint32_t>(head_);
    }

    dma::DmaView memory_;
    volatile std::uint32_t* doorbell_;
    std::size_t depth_;
    std::size_t head_{};

    std::uint8_t expected_bit_;
};
}  // namespace nvme
