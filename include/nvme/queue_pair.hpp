#pragma once

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/command.hpp"
#include "nvme/completion_queue.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/registers.hpp"
#include "nvme/submission_queue.hpp"
#include "utils.hpp"

namespace nvme {

// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
//        The Admin Submission Queue and Admin Completion Queue are required to be in physically
//        contiguous memory.

class QueuePair {
public:
    QueuePair(QueueId qid, dma::DmaHeap& heap, ControllerRegisters& cregs, std::size_t depth);

    ~QueuePair() = default;

    QueuePair(const QueuePair&) = delete;
    QueuePair& operator=(const QueuePair&) = delete;
    
    QueuePair(QueuePair&&) noexcept = default;
    QueuePair& operator=(QueuePair&&) noexcept = default;

    QueueId id() const;

    std::uint64_t sq_iova() const;
    std::uint64_t cq_iova() const;

    dma::DmaView sq_region() const;
    dma::DmaView cq_region() const;

    SubmissionQueue& sq();
    CompletionQueue& cq();

private:
    static std::size_t sq_region_bytes(std::size_t depth) {
        return utils::page_round_up(depth * sizeof(wire::SubmissionEntry));
    }

    static std::size_t cq_region_bytes(std::size_t depth) {
        return utils::page_round_up(depth * sizeof(wire::CompletionEntry));
    }

    QueueId qid_;
    dma::DmaBuffer memory_;
    ControllerRegisters::Doorbells dbs_;
    std::size_t sq_region_bytes_;
    std::size_t cq_region_bytes_;
    SubmissionQueue sq_;
    CompletionQueue cq_;
};

}  // namespace nvme
