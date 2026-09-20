#include "nvme/queue_pair.hpp"

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/completion_queue.hpp"
#include "nvme/registers.hpp"
#include "nvme/submission_queue.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace nvme {

std::size_t
QueuePair::region_bytes(std::size_t depth, std::size_t entry_size, std::size_t page_size) {
    if (depth > std::numeric_limits<std::size_t>::max() / entry_size) {
        throw std::overflow_error("Queue memory size exceeds address space");
    }
    const auto bytes = utils::align_up(depth * entry_size, page_size);
    if (bytes > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error("Queue pair memory size exceeds address space");
    }
    return bytes;
}

QueuePair::QueuePair(
    QueueId qid,
    dma::DmaHeap& heap,
    ControllerRegisters& cregs,
    std::size_t depth,
    const ControllerConfiguration& configuration
)
    : qid_{qid},
      sq_region_bytes_{
          region_bytes(depth, sizeof(wire::SubmissionEntry), configuration.nvme_page_size)
      },
      cq_region_bytes_{
          region_bytes(depth, sizeof(wire::CompletionEntry), configuration.nvme_page_size)
      },
      memory_{heap.alloc(sq_region_bytes_ + cq_region_bytes_, configuration.nvme_page_size)},
      dbs_{cregs.doorbells(qid.value)},
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
