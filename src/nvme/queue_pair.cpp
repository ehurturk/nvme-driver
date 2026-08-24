#include "nvme/queue_pair.hpp"

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/completion_queue.hpp"
#include "nvme/registers.hpp"
#include "nvme/submission_queue.hpp"
#include "nvme/traits.hpp"

#include <cstdint>

namespace nvme {

AdminQueuePair::AdminQueuePair(dma::DmaHeap& heap, ControllerRegisters::Doorbells doorbells)
    : memory_{heap.alloc(sq_region_bytes() + cq_region_bytes())},
      sq_{dma::DmaView{memory_}.subview(0, sq_region_bytes()), doorbells.sq_tail},
      cq_{dma::DmaView{memory_}.subview(sq_region_bytes(), cq_region_bytes()), doorbells.cq_head} {
}

std::uint64_t AdminQueuePair::sq_iova() const {
    return memory_.iova();
}

std::uint64_t AdminQueuePair::cq_iova() const {
    return memory_.iova() + sq_region_bytes();
}

SubmissionQueue<AdminQueueKind>& AdminQueuePair::sq() {
    return sq_;
}

CompletionQueue<AdminQueueKind>& AdminQueuePair::cq() {
    return cq_;
}

}  // namespace nvme
