#pragma once

#include <vector>

#include "submission_queue.hpp"
#include "completion_queue.hpp"
#include "traits.hpp"

namespace nvme
{

// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
class AdminQueuePair
{
    private:
        SubmissionQueue<AdminQueueKind> sq_;
        CompletionQueue<AdminQueueKind> cq_;
};


class IOQueuePair
{
    private:
        std::vector<SubmissionQueue<AdminQueueKind>> sqs_;
        CompletionQueue<AdminQueueKind> cq_;

};

} // namespace nvme
