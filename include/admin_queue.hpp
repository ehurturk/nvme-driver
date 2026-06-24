#pragma once

#include "queue_pair.hpp"
#include "traits.hpp"

namespace nvme
{

struct AdminSubmissionEntry {

};

struct AdminCompletionEntry {
};

using AdminQueuePair = QueuePair<AdminQueueKind>;

} // namespace nvme
