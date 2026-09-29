#pragma once

#include "nvme/admin/features.hpp"
#include "nvme/admin/get_log_page.hpp"
#include "nvme/admin/identify.hpp"
#include "nvme/command.hpp"

#include <cstdint>
#include <vector>

namespace nvme::admin {

Result<IdentifyControllerData> identify_controller(Controller& controller);

Result<IdentifyNamespaceData> identify_namespace(Controller& controller, std::uint32_t nsid);

Result<std::vector<std::uint32_t>> list_active_namespaces(Controller& controller);

Result<SmartLogData> read_smart_log(Controller& controller);

Result<std::vector<ErrorLogEntry>> read_error_log(Controller& controller, std::uint32_t max_entries = 64);

Result<features::NumberOfQueues::Completion>
set_number_of_queues(Controller& controller, std::uint16_t nsq, std::uint16_t ncq);

// creates an io queue pair on the controller and returns the id it was given
Result<QueueId> create_io_queue_pair(Controller& controller, std::uint16_t qsize);

}  // namespace nvme::admin
