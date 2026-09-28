#pragma once

#include "dma/buffer.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/spec.hpp"

#include <atomic>
#include <bit>
#include <cassert>
#include <cstring>
#include <print>

namespace nvme {

class SubmissionQueue {
public:
    using Entry = wire::SubmissionEntry;

    static_assert(sizeof(Entry) == 64, "Entry size must be 64 bytes.");

    static_assert(std::is_trivially_copyable_v<Entry>);
    static_assert(std::is_standard_layout_v<Entry>);

    SubmissionQueue(dma::DmaView dma_region, volatile std::uint32_t* tail_doorbell, std::size_t depth)
        : dma_region_{dma_region}, doorbell_{tail_doorbell}, depth_{depth} {
        assert(std::has_single_bit(depth_) && "Queue size must be a power of 2.");
        assert(depth_ >= constants::MIN_QUEUE_DEPTH);
        assert(depth_ <= constants::MAX_IO_QUEUE_DEPTH);
        assert(dma_region_.size >= depth_ * sizeof(Entry));
    }

    // writes + commits a single entry to the queue
    bool push(const Entry& e) {
        if (!write(e)) {
            return false;
        }

        commit();
        return true;
    }

    // writes but does not ring the doorbell
    bool write(const Entry& e) {
        if (full()) {
            return false;
        }

        // copy entry
        // TODO: use std::uninitialized_copy/move based on value category such that we can
        //       prevent a copy in rvalue temporaries
        auto* dst = dma_region_.uva + (tail_ * sizeof(Entry));
        std::memcpy(dst, &e, sizeof(Entry));

        tail_ = (tail_ + 1) & (depth_ - 1);
        return true;
    }

    // rings the doorbell
    void commit() {
        // ensure writes are done before the doorbell update
        std::atomic_thread_fence(std::memory_order_release);

        ring_tail_doorbell();
    }

    // NOTE: should be called when a completion is done in the
    // completion queue
    void advance_head(std::size_t head) {
        if (head >= depth_) {
            std::println("[ERROR]: Invalid NVMe SQ head.");
            return;
        }
        head_ = head;
    }

    bool full() const {
        return ((tail_ + 1) & (depth_ - 1)) == head_;
    }

    std::size_t depth() const {
        return depth_;
    }

    std::size_t head() const {
        return head_;
    }

    std::size_t tail() const {
        return tail_;
    }

private:
    void ring_tail_doorbell() {
        *doorbell_ = static_cast<std::uint32_t>(tail_);
    }

    dma::DmaView dma_region_;
    volatile std::uint32_t* doorbell_;
    std::size_t depth_;
    std::size_t tail_{};
    std::size_t head_{};
};

}  // namespace nvme
