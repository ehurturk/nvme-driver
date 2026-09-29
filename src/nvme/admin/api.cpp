#include "nvme/admin/api.hpp"

#include "nvme/admin/create_io_queue.hpp"
#include "nvme/admin/delete_io_queue.hpp"
#include "nvme/admin/set_features.hpp"
#include "nvme/controller.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <utility>

namespace nvme::admin {

Result<IdentifyControllerData> identify_controller(Controller& controller) {
    return controller.execute(commands::IdentifyControllerType{});
}

Result<IdentifyNamespaceData> identify_namespace(Controller& controller, std::uint32_t nsid) {
    return controller.execute(commands::IdentifyNamespaceType{nsid});
}

Result<std::vector<std::uint32_t>> list_active_namespaces(Controller& controller) {
    std::vector<std::uint32_t> nsids;

    std::uint32_t cursor = 0;
    while (true) {
        auto page = controller.execute(commands::IdentifyActiveNamespaceIdListType{cursor});
        if (!page) {
            return std::unexpected{std::move(page.error())};
        }

        const auto ids = page->nsids();
        nsids.insert(nsids.end(), ids.begin(), ids.end());

        // NSID values >= FFFFFFFEh are not valid cursors for this CNS
        if (!page->full() || nsids.back() >= 0xFFFFFFFE) {
            break;
        }
        cursor = nsids.back();
    }

    return nsids;
}

Result<SmartLogData> read_smart_log(Controller& controller) {
    return controller.execute(commands::GetLogPage{LogPageId::SmartHealth, SmartLogData::SIZE / 4})
        .transform([](const std::vector<std::byte>& bytes) {
            return SmartLogData{bytes.data()};
        });
}

Result<std::vector<ErrorLogEntry>> read_error_log(Controller& controller, std::uint32_t max_entries) {
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

    const auto num_dwords = static_cast<std::uint32_t>(max_entries * (ErrorLogEntry::SIZE / 4));
    return controller.execute(commands::GetLogPage{LogPageId::Error, num_dwords})
        .transform([&](const std::vector<std::byte>& bytes) {
            std::vector<ErrorLogEntry> entries;
            for (std::uint32_t i = 0; i < max_entries; ++i) {
                ErrorLogEntry entry{bytes.data() + (i * ErrorLogEntry::SIZE)};
                if (entry.error_count() != 0) {
                    entries.push_back(entry);
                }
            }
            return entries;
        });
}

Result<features::NumberOfQueues::Completion>
set_number_of_queues(Controller& controller, std::uint16_t nsq, std::uint16_t ncq) {
    using SetNumberOfQueues = commands::SetFeatures<features::NumberOfQueues>;

    return controller.execute(SetNumberOfQueues{features::NumberOfQueues{nsq, ncq}})
        .transform([&](const features::NumberOfQueues::Completion& allocated) {
            controller.grant_io_queues(std::min(allocated.allocated_sq, allocated.allocated_cq));
            return allocated;
        });
}

Result<QueueId> create_io_queue_pair(Controller& controller, std::uint16_t qsize) {
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

    auto qid = controller.add_io_queue_pair(qsize);
    if (!qid) {
        return std::unexpected{qid.error()};
    }

    const Queue* pair = controller.io_queue(*qid);

    // Create IO CQ
    auto created_cq = controller.execute(commands::CreateIoCq{pair->cq_region(), *qid, qsize});
    if (!created_cq) {
        if (created_cq.error().status) {
            controller.remove_io_queue_pair(*qid);
        }
        return std::unexpected{std::move(created_cq.error())};
    }

    auto created_sq =
        controller.execute(commands::CreateIoSq{pair->sq_region(), *qid, CompletionQueueId{qid->value}, qsize});
    if (!created_sq) {
        if (!created_sq.error().status) {
            return std::unexpected{std::move(created_sq.error())};
        }

        auto deleted_cq = controller.execute(commands::DeleteIoCq{*qid});
        if (!deleted_cq) {
            // the CQ is still live on the device, so its memory must stay mapped
            return std::unexpected{Error{
                .reason = std::format(
                    "{}, cleanup failed. IO CQ {} left allocated: {}",
                    created_sq.error().reason,
                    qid->value,
                    deleted_cq.error().reason
                ),
                .severity = Error::Severity::ERROR
            }};
        }

        controller.remove_io_queue_pair(*qid);
        return std::unexpected{std::move(created_sq.error())};
    }

    return *qid;
}

}  // namespace nvme::admin
