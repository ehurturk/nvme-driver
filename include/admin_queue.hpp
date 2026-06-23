#pragma once

#include "queue_pair.hpp"

namespace nvme
{

struct AdminSubmissionEntry {
};

struct AdminCompletionEntry {
};

struct AdminQueue : public Queue<AdminQueue> {
    void submit_impl()
    {
    }
};

using AdminQueuePair = QueuePair<AdminQueue>;

} // namespace nvme
