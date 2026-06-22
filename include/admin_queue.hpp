#pragma once

#include "queue_pair.hpp"

namespace nvme
{

struct AdminSubmissionQueue : public SubmissionQueue<AdminSubmissionQueue> {
    void submit_impl();
};

using AdminQueuePair = QueuePair<AdminSubmissionQueue>;

struct AdminQueue
{
    AdminQueuePair qp_;
};

} // namespace nvme
