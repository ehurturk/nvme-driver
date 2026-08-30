#include "nvme/queue_pair.hpp"

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/completion_queue.hpp"
#include "nvme/registers.hpp"
#include "nvme/submission_queue.hpp"

#include <cstdint>

namespace nvme {

QueuePair::QueuePair(QueueId qid, dma::DmaHeap& heap, ControllerRegisters& cregs, std::size_t depth)
    : qid_{qid},
      memory_{heap.alloc(sq_region_bytes(depth) + cq_region_bytes(depth))},
      dbs_{cregs.doorbells(qid.value)},
      sq_region_bytes_{sq_region_bytes(depth)},
      cq_region_bytes_{cq_region_bytes(depth)},
      sq_{dma::DmaView{memory_}.subview(0, sq_region_bytes_), dbs_.sq_tail, depth},
      cq_{dma::DmaView{memory_}.subview(sq_region_bytes_, cq_region_bytes_), dbs_.cq_head, depth} {
}

QueueId QueuePair::id() const {
    return qid_;
}

std::uint64_t QueuePair::sq_iova() const {
    return memory_.iova();
}

std::uint64_t QueuePair::cq_iova() const {
    return memory_.iova() + sq_region_bytes_;
}

dma::DmaView QueuePair::sq_region() const {
    return dma::DmaView{memory_}.subview(0, sq_region_bytes_);
}

dma::DmaView QueuePair::cq_region() const {
    return dma::DmaView{memory_}.subview(sq_region_bytes_, cq_region_bytes_);
}

SubmissionQueue& QueuePair::sq() {
    return sq_;
}

CompletionQueue& QueuePair::cq() {
    return cq_;
}

}  // namespace nvme
