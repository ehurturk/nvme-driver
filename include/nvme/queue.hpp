#pragma once

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/command.hpp"
#include "nvme/completion_queue.hpp"
#include "nvme/controller_configuration.hpp"
#include "nvme/queue_entry.hpp"
#include "nvme/registers.hpp"
#include "nvme/request_table.hpp"
#include "nvme/submission_queue.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>

namespace nvme {

// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
//        The Admin Submission Queue and Admin Completion Queue are required to be in physically
//        contiguous memory.

class Queue {
public:
    Queue(
        QueueId qid,
        dma::DmaHeap& heap,
        ControllerRegisters& cregs,
        std::size_t depth,
        const ControllerConfiguration& configuration
    );

    ~Queue() = default;

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

    QueueId id() const;

    std::uint64_t sq_iova() const;
    std::uint64_t cq_iova() const;

    dma::DmaView sq_region() const;
    dma::DmaView cq_region() const;

    /// @brief Allocates a CID for the given command and pushes the built SubmissionEntry to the submission queue
    template<CommandDescriptor Command>
    Result<Cid> submit(const Command& command) {
        if (sq_.full()) {
            return std::unexpected{Error{
                .reason = std::format("Submission queue {} is full", qid_.value),
                .severity = Error::Severity::ERROR
            }};
        }

        // SQ slots free up as soon as the CQ is drained, but a CID stays held until its
        // CommandPending takes the result, so CIDs can run out while the SQ has room
        const std::optional<Cid> allocated = requests_.acquire();
        if (!allocated) {
            return std::unexpected{Error{
                .reason =
                    std::format("No free command identifiers on queue {} (all held by pending commands)", qid_.value),
                .severity = Error::Severity::ERROR
            }};
        }
        const Cid cmd_id = *allocated;

        if (!sq_.push(command.build(cmd_id.value))) {
            requests_.cancel(cmd_id);
            return std::unexpected{Error{
                .reason = std::format("Submission queue {} rejected the command", qid_.value),
                .severity = Error::Severity::ERROR
            }};
        }

        return cmd_id;
    }

    /// @brief Wait until the given CID command is completed and placed in the completion queue. Waiting more than the
    /// timeout will result in an Error without an empty completion entry returned
    Result<wire::CompletionEntry> wait(Cid cid);

    /// @brief Polls the completion queue until it finds the command CID. If it finds any existing completed commands
    /// along the way, marks them as completed as well, so performs a sweep poll
    std::optional<Result<wire::CompletionEntry>> poll(Cid cid);

    /// @brief Discards the given CID
    void abandon(Cid cid);

private:
    static std::size_t region_bytes(std::size_t depth, std::size_t entry_size, std::size_t page_size);

    std::optional<Error> write_complete(const wire::CompletionEntry& completion);

    QueueId qid_;
    std::size_t sq_region_bytes_;
    std::size_t cq_region_bytes_;
    dma::DmaBuffer memory_;
    SubmissionQueue sq_;
    CompletionQueue cq_;
    RequestTable requests_;
};

}  // namespace nvme
