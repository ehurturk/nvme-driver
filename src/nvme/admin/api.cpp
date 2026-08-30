#include "nvme/admin/api.hpp"

#include "nvme/admin/create_io_queue.hpp"
#include "nvme/admin/delete_io_queue.hpp"
#include "nvme/admin/set_features.hpp"
#include "nvme/controller.hpp"
#include "nvme/queue_entry.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <string_view>

namespace nvme::admin {

namespace {

Error completion_error(std::string_view what, const wire::CompletionEntry& completion) {
    return Error{
        .reason = std::format("{} completed with error status: {}", what, completion.status),
        .severity = Error::Severity::ERROR
    };
}

}  // namespace

Result<IdentifyControllerData> identify_controller(Controller& controller, dma::DmaHeap& heap) {
    dma::DmaBuffer buf = heap.alloc(IdentifyControllerData::SIZE);
    const commands::IdentifyControllerType cmd{dma::DmaView{buf}};

    auto done = controller.execute_admin_command(cmd);
    if (!done) {
        return std::unexpected{done.error()};
    }
    if (!done->ok()) {
        return std::unexpected{completion_error("Identify Controller", *done)};
    }

    return cmd.data();
}

Result<IdentifyNamespaceData>
identify_namespace(Controller& controller, dma::DmaHeap& heap, std::uint32_t nsid) {
    dma::DmaBuffer buf = heap.alloc(IdentifyNamespaceData::SIZE);
    const commands::IdentifyNamespaceType cmd{dma::DmaView{buf}, nsid};

    auto done = controller.execute_admin_command(cmd);
    if (!done) {
        return std::unexpected{done.error()};
    }
    if (!done->ok()) {
        return std::unexpected{completion_error(std::format("Identify Namespace {}", nsid), *done)};
    }

    return cmd.data();
}

Result<std::vector<std::uint32_t>>
list_active_namespaces(Controller& controller, dma::DmaHeap& heap) {
    std::vector<std::uint32_t> nsids;
    dma::DmaBuffer buf = heap.alloc(IdentifyActiveNamespaceIdListData::SIZE);

    std::uint32_t cursor = 0;
    while (true) {
        const commands::IdentifyActiveNamespaceIdListType cmd{dma::DmaView{buf}, cursor};

        auto done = controller.execute_admin_command(cmd);
        if (!done) {
            return std::unexpected{done.error()};
        }
        if (!done->ok()) {
            return std::unexpected{completion_error("Identify Active Namespace ID List", *done)};
        }

        const auto page = cmd.data();
        const auto ids = page.nsids();
        nsids.insert(nsids.end(), ids.begin(), ids.end());

        // NSID values >= FFFFFFFEh are not valid cursors for this CNS
        if (!page.full() || nsids.back() >= 0xFFFFFFFE) {
            break;
        }
        cursor = nsids.back();
    }

    return nsids;
}

Result<SmartLogData> read_smart_log(Controller& controller, dma::DmaHeap& heap) {
    dma::DmaBuffer buf = heap.alloc(SmartLogData::SIZE);

    auto log = controller.execute_admin_command(
        commands::GetLogPage{dma::DmaView{buf}, LogPageId::SmartHealth, SmartLogData::SIZE / 4}
    );
    if (!log) {
        return std::unexpected{log.error()};
    }
    if (!log->ok()) {
        return std::unexpected{completion_error("Get Log Page (SMART)", *log)};
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
            dma::DmaView{buf},
            LogPageId::Error,
            static_cast<std::uint32_t>(max_entries * (ErrorLogEntry::SIZE / 4))
        }
    );
    if (!log) {
        return std::unexpected{log.error()};
    }
    if (!log->ok()) {
        return std::unexpected{completion_error("Get Log Page (Error Information)", *log)};
    }

    std::vector<ErrorLogEntry> entries;
    for (std::uint32_t i = 0; i < max_entries; ++i) {
        ErrorLogEntry entry{buf.uva() + (i * ErrorLogEntry::SIZE)};
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
        controller.submit_admin_command(SetNumberOfQueues{features::NumberOfQueues{nsq, ncq}});
    if (!submitted) {
        return std::unexpected{submitted.error()};
    }

    auto allocated = submitted->result();
    if (allocated) {
        controller.grant_io_queues(std::min(allocated->allocated_sq, allocated->allocated_cq));
    }

    return allocated;
}

Result<QueuePair>
create_io_queue_pair(Controller& controller, dma::DmaHeap& heap, std::uint16_t qsize) {
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

    auto qid = controller.allocate_io_qid();
    if (!qid) {
        return std::unexpected{qid.error()};
    }

    QueuePair pair = controller.make_io_queue_pair(*qid, heap, qsize);

    auto created_cq =
        controller.execute_admin_command(commands::CreateIoCq{pair.cq_region(), *qid, qsize});
    if (!created_cq || !created_cq->ok()) {
        controller.release_io_qid(*qid);
        if (!created_cq) {
            return std::unexpected{created_cq.error()};
        }
        return std::unexpected{
            completion_error(std::format("Create IO CQ {}", qid->value), *created_cq)
        };
    }

    auto created_sq = controller.execute_admin_command(
        commands::CreateIoSq{pair.sq_region(), *qid, CompletionQueueId{qid->value}, qsize}
    );
    if (!created_sq || !created_sq->ok()) {
        auto unused = controller.execute_admin_command(commands::DeleteIoCq{*qid});
        controller.release_io_qid(*qid);
        if (!created_sq) {
            return std::unexpected{created_sq.error()};
        }
        return std::unexpected{
            completion_error(std::format("Create IO SQ {}", qid->value), *created_sq)
        };
    }

    return pair;
}

}  // namespace nvme::admin
