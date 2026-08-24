#include "nvme/admin/api.hpp"

#include "nvme/admin/create_io_queue.hpp"
#include "nvme/admin/set_features.hpp"
#include "nvme/controller.hpp"
#include "nvme/queue_entry.hpp"

#include <cstdint>
#include <expected>
#include <format>
#include <utility>

namespace nvme::admin {

Result<IdentifyControllerData> identify_controller(Controller& controller, dma::DmaHeap& heap) {
    dma::DmaBuffer result_buffer = heap.alloc(IdentifyControllerData::SIZE);

    auto identify = controller.execute_admin_command(
        commands::IdentifyControllerType{.prp1 = result_buffer.iova()}
    );
    if (!identify) {
        return std::unexpected{identify.error()};
    }
    if (!identify->ok()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Identify Controller completed with error status: {}",
                identify->status
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    return IdentifyControllerData{result_buffer.uva()};
}

Result<IdentifyNamespaceData>
identify_namespace(Controller& controller, dma::DmaHeap& heap, std::uint32_t nsid) {
    dma::DmaBuffer result_buffer = heap.alloc(IdentifyNamespaceData::SIZE);

    auto identify = controller.execute_admin_command(
        commands::IdentifyNamespaceType{.prp1 = result_buffer.iova(), .nsid = nsid}
    );
    if (!identify) {
        return std::unexpected{identify.error()};
    }
    if (!identify->ok()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Identify Namespace {} completed with error status: {}",
                nsid,
                identify->status
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    return IdentifyNamespaceData{result_buffer.uva()};
}

Result<SmartLogData> read_smart_log(Controller& controller, dma::DmaHeap& heap) {
    dma::DmaBuffer buf = heap.alloc(SmartLogData::SIZE);

    auto log = controller.execute_admin_command(
        commands::GetLogPage{
            .prp1 = buf.iova(),
            .lid = LogPageId::SmartHealth,
            .num_dwords = SmartLogData::SIZE / 4,
        }
    );
    
    if (!log) {
        return std::unexpected{log.error()};
    }

    if (!log->ok()) {
        return std::unexpected{Error{
            .reason =
                std::format("Get Log Page (SMART) completed with error status: {}", log->status),
            .severity = Error::Severity::ERROR
        }};
    }

    return SmartLogData{buf.uva()};
}

Result<std::vector<ErrorLogEntry>>
read_error_log(Controller& controller, dma::DmaHeap& heap, std::uint32_t max_entries) {
    if (max_entries == 0 || max_entries * ErrorLogEntry::SIZE > 4096) {
        return std::unexpected{Error{
            .reason = std::format(
                "Error log request of {} entries exceeds the single-page (PRP1-only) limit of {}",
                max_entries,
                4096 / ErrorLogEntry::SIZE
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    dma::DmaBuffer buf = heap.alloc(max_entries * ErrorLogEntry::SIZE);

    auto log = controller.execute_admin_command(
        commands::GetLogPage{
            .prp1 = buf.iova(),
            .lid = LogPageId::Error,
            .num_dwords = static_cast<std::uint32_t>(max_entries * (ErrorLogEntry::SIZE / 4)),
        }
    );
    if (!log) {
        return std::unexpected{log.error()};
    }
    if (!log->ok()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Get Log Page (Error Information) completed with error status: {}",
                log->status
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    std::vector<ErrorLogEntry> entries;
    for (std::uint32_t i = 0; i < max_entries; ++i) {
        ErrorLogEntry entry{buf.uva() + i * ErrorLogEntry::SIZE};
        if (entry.error_count() != 0) {
            entries.push_back(entry);
        }
    }

    return entries;
}

Result<features::NumberOfQueues::Completion>
set_number_of_queues(Controller& controller, std::uint16_t nsq, std::uint16_t ncq) {
    using SetNumberOfQueues = commands::SetFeatures<features::NumberOfQueues>;

    auto submitted =
        controller.submit_admin_command(SetNumberOfQueues{.feature = {.nsq = nsq, .ncq = ncq}});
    if (!submitted) {
        return std::unexpected{submitted.error()};
    }

    return submitted->result();
}

Result<dma::DmaBuffer>
create_io_cq(Controller& controller, dma::DmaHeap& heap, std::uint16_t qid, std::uint16_t qsize) {
    if (qsize > controller.max_io_queue_entries()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Queue size {} exceeds CAP.MQES limit of {} entries",
                qsize,
                controller.max_io_queue_entries()
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    dma::DmaBuffer buf = heap.alloc(qsize * sizeof(wire::CompletionEntry));

    auto created = controller.execute_admin_command(
        commands::CreateIoCq{
            .prp1 = buf.iova(),
            .qid = qid,
            .qsize = qsize,
            .interrupt_vector = 0,
            .interrupts_enabled = false,
            .physically_contiguous = true,
        }
    );
    if (!created) {
        return std::unexpected{created.error()};
    }
    if (!created->ok()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Create IO CQ {} completed with error status: {}",
                qid,
                created->status
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    return buf;
}

Result<dma::DmaBuffer> create_io_sq(
    Controller& controller,
    dma::DmaHeap& heap,
    std::uint16_t qid,
    std::uint16_t cqid,
    std::uint16_t qsize
) {
    if (qsize > controller.max_io_queue_entries()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Queue size {} exceeds CAP.MQES limit of {} entries",
                qsize,
                controller.max_io_queue_entries()
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    dma::DmaBuffer buf = heap.alloc(qsize * sizeof(wire::SubmissionEntry));

    auto created = controller.execute_admin_command(
        commands::CreateIoSq{
            .prp1 = buf.iova(),
            .qid = qid,
            .qsize = qsize,
            .cqid = cqid,
            .physically_contiguous = true,
        }
    );
    if (!created) {
        return std::unexpected{created.error()};
    }
    if (!created->ok()) {
        return std::unexpected{Error{
            .reason = std::format(
                "Create IO SQ {} completed with error status: {}",
                qid,
                created->status
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    return buf;
}

}  // namespace nvme::admin
