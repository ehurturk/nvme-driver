#pragma once

#include <vector>

#include "submission_queue.hpp"
#include "completion_queue.hpp"

namespace nvme
{
// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
template <class Kind> struct QueuePair {
    std::vector<SubmissionQueue<Kind>> submission_queues;
    CompletionQueue<Kind> completion_queue;
};

using AdminQueuePair = QueuePair<AdminQueueKind>;


} // namespace nvme
