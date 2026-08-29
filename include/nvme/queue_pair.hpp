#pragma once

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/completion_queue.hpp"
#include "nvme/registers.hpp"
#include "nvme/spec.hpp"
#include "nvme/submission_queue.hpp"
#include "nvme/traits.hpp"
#include "nvme/command.hpp"
#include "utils.hpp"

#include <vector>

namespace nvme {

class QueuePair {
protected:
    QueuePair(QueueId qid, dma::DmaHeap& heap, ControllerRegisters& cregs, std::size_t size);

    dma::DmaBuffer memory_;
    ControllerRegisters::Doorbells dbs_;
};


// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
//        The Admin Submission Queue and Admin Completion Queue are required to be in physically
//        contiguous memory.

class AdminQueuePair : protected QueuePair {
public:
    AdminQueuePair(dma::DmaHeap& heap, ControllerRegisters& cregs);

    ~AdminQueuePair() = default;

    AdminQueuePair(const AdminQueuePair&) = delete;
    AdminQueuePair& operator=(const AdminQueuePair&) = delete;
    AdminQueuePair(AdminQueuePair&&) = delete;
    AdminQueuePair& operator=(AdminQueuePair&&) = delete;

    std::uint64_t sq_iova() const;
    std::uint64_t cq_iova() const;

    SubmissionQueue<AdminQueueKind>& sq();
    CompletionQueue<AdminQueueKind>& cq();

private:
    static constexpr std::size_t sq_bytes() {
        return constants::AQ_DEPTH * (std::size_t{1} << constants::ADM_SQES);
    }

    static constexpr std::size_t cq_bytes() {
        return constants::AQ_DEPTH * (std::size_t{1} << constants::ADM_CQES);
    }

    static constexpr std::size_t sq_region_bytes() {
        return utils::page_round_up(sq_bytes());
    }

    static constexpr std::size_t cq_region_bytes() {
        return utils::page_round_up(cq_bytes());
    }

    SubmissionQueue<AdminQueueKind> sq_;
    CompletionQueue<AdminQueueKind> cq_;
};

class IOQueuePair: protected QueuePair {
private:
    std::vector<SubmissionQueue<IOQueueKind>> sqs_;
    CompletionQueue<IOQueueKind> cq_;
};

}  // namespace nvme
