#pragma once

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/admin/features.hpp"
#include "nvme/admin/get_log_page.hpp"
#include "nvme/admin/identify.hpp"
#include "nvme/command.hpp"

#include <cstdint>
#include <vector>

namespace nvme::admin {

Result<IdentifyControllerData> identify_controller(Controller& controller, dma::DmaHeap& heap);

Result<IdentifyNamespaceData>
identify_namespace(Controller& controller, dma::DmaHeap& heap, std::uint32_t nsid);

Result<std::vector<std::uint32_t>>
list_active_namespaces(Controller& controller, dma::DmaHeap& heap);

Result<SmartLogData> read_smart_log(Controller& controller, dma::DmaHeap& heap);

Result<std::vector<ErrorLogEntry>>
read_error_log(Controller& controller, dma::DmaHeap& heap, std::uint32_t max_entries = 64);

Result<features::NumberOfQueues::Completion>
set_number_of_queues(Controller& controller, std::uint16_t nsq, std::uint16_t ncq);

Result<dma::DmaBuffer>
create_io_cq(Controller& controller, dma::DmaHeap& heap, QueueId qid, std::uint16_t qsize);

Result<dma::DmaBuffer> create_io_sq(
    Controller& controller,
    dma::DmaHeap& heap,
    QueueId qid,
    CompletionQueueId cqid,
    std::uint16_t qsize
);

}  // namespace nvme::admin
