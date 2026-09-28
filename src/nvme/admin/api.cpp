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
#include <string>
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
    dma::DmaBuffer buf = heap.alloc(IdentifyControllerData::SIZE, controller.configuration().nvme_page_size);
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

Result<IdentifyNamespaceData> identify_namespace(Controller& controller, dma::DmaHeap& heap, std::uint32_t nsid) {
    dma::DmaBuffer buf = heap.alloc(IdentifyNamespaceData::SIZE, controller.configuration().nvme_page_size);
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

Result<std::vector<std::uint32_t>> list_active_namespaces(Controller& controller, dma::DmaHeap& heap) {
    std::vector<std::uint32_t> nsids;
    dma::DmaBuffer buf = heap.alloc(IdentifyActiveNamespaceIdListData::SIZE, controller.configuration().nvme_page_size);

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
    dma::DmaBuffer buf = heap.alloc(SmartLogData::SIZE, controller.configuration().nvme_page_size);

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
    const auto page_size = controller.configuration().nvme_page_size;
    if (max_entries == 0 || max_entries > page_size / ErrorLogEntry::SIZE) {
        return std::unexpected{Error{
            .reason = std::format(
                "Error log request of {} entries exceeds the single-page (PRP1-only) limit of {}",
                max_entries,
                page_size / ErrorLogEntry::SIZE
            ),
            .severity = Error::Severity::ERROR
        }};
    }

    dma::DmaBuffer buf = heap.alloc(max_entries * ErrorLogEntry::SIZE, page_size);

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

    auto submitted = controller.submit_admin_command(SetNumberOfQueues{features::NumberOfQueues{nsq, ncq}});
    if (!submitted) {
        return std::unexpected{submitted.error()};
    }

    auto allocated = submitted->result();
    if (allocated) {
        controller.grant_io_queues(std::min(allocated->allocated_sq, allocated->allocated_cq));
    }

    return allocated;
}

Result<QueueId> create_io_queue_pair(Controller& controller, dma::DmaHeap& heap, std::uint16_t qsize) {
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

    auto qid = controller.add_io_queue_pair(heap, qsize);
    if (!qid) {
        return std::unexpected{qid.error()};
    }

    const Queue* pair = controller.io_queue(*qid);

    // Create IO CQ
    auto cq_ready =
        controller.execute_admin_command(commands::CreateIoCq{pair->cq_region(), *qid, qsize})
            .and_then([&](const auto& completion) -> Result<wire::SubmissionEntry> {
                if (completion.ok()) {
                    return {};
                }

                controller.remove_io_queue_pair(*qid);
                return std::unexpected{completion_error(std::format("Create IO CQ {}", qid->value), completion)};
            });

    if (!cq_ready) {
        return std::unexpected{cq_ready.error()};
    }

    auto created_sq = controller.execute_admin_command(
        commands::CreateIoSq{pair->sq_region(), *qid, CompletionQueueId{qid->value}, qsize}
    );

    if (!created_sq) {
        return std::unexpected{created_sq.error()};
    }

    if (!created_sq->ok()) {
        const Error sq_error = completion_error(std::format("Create IO SQ {}", qid->value), *created_sq);

        auto deleted_cq = controller.execute_admin_command(commands::DeleteIoCq{*qid});
        if (!deleted_cq || !deleted_cq->ok()) {
            // the CQ is still live on the device, so its memory must stay mapped
            const std::string why =
                deleted_cq ? completion_error(std::format("Delete IO CQ {}", qid->value), *deleted_cq).reason
                           : deleted_cq.error().reason;
            return std::unexpected{Error{
                .reason =
                    std::format("{}, cleanup failed. IO CQ {} left allocated: {}", sq_error.reason, qid->value, why),
                .severity = Error::Severity::ERROR
            }};
        }

        controller.remove_io_queue_pair(*qid);
        return std::unexpected{sq_error};
    }

    return *qid;
}

}  // namespace nvme::admin
