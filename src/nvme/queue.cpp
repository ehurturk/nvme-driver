#include "nvme/queue.hpp"

#include "dma/buffer.hpp"
#include "dma/heap.hpp"
#include "nvme/spec.hpp"
#include "utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
nvme::Error not_in_flight(nvme::Cid cid) {
    return nvme::Error{
        .reason = std::format("Polling for CID {}, which is not in flight", cid),
        .severity = nvme::Error::Severity::ERROR
    };
}
}  // namespace

namespace nvme {

std::size_t Queue::region_bytes(std::size_t depth, std::size_t entry_size, std::size_t page_size) {
    if (depth > std::numeric_limits<std::size_t>::max() / entry_size) {
        throw std::overflow_error("Queue memory size exceeds address space");
    }
    const auto bytes = utils::align_up(depth * entry_size, page_size);
    if (bytes > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error("Queue pair memory size exceeds address space");
    }
    return bytes;
}

Queue::Queue(
    QueueId qid,
    dma::DmaHeap& heap,
    ControllerRegisters& cregs,
    std::size_t depth,
    const ControllerConfiguration& configuration
)
    : qid_{qid},
      sq_region_bytes_{region_bytes(depth, sizeof(wire::SubmissionEntry), configuration.nvme_page_size)},
      cq_region_bytes_{region_bytes(depth, sizeof(wire::CompletionEntry), configuration.nvme_page_size)},
      memory_{heap.alloc(sq_region_bytes_ + cq_region_bytes_, configuration.nvme_page_size)},
      sq_{sq_region(), cregs.doorbells(qid.value).sq_tail, depth},
      cq_{cq_region(), cregs.doorbells(qid.value).cq_head, depth},
      requests_{static_cast<Cid::value_type>(std::min<std::size_t>(depth - 1, 0xFFFE))} {
}

QueueId Queue::id() const {
    return qid_;
}

std::uint64_t Queue::sq_iova() const {
    return memory_.iova();
}

std::uint64_t Queue::cq_iova() const {
    return memory_.iova() + sq_region_bytes_;
}

dma::DmaView Queue::sq_region() const {
    return dma::DmaView{memory_}.subview(0, sq_region_bytes_);
}

dma::DmaView Queue::cq_region() const {
    return dma::DmaView{memory_}.subview(sq_region_bytes_, cq_region_bytes_);
}

Result<wire::CompletionEntry> Queue::wait(Cid cid) {
    if (!requests_.pending(cid)) {
        return std::unexpected{not_in_flight(cid)};
    }

    const auto timeout =
        qid_.value == constants::ADMIN_QID.value ? constants::ADMIN_CMD_TIMEOUT : constants::IO_CMD_TIMEOUT;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        if (auto done = requests_.take(cid)) {
            return *done;
        }

        const std::optional<wire::CompletionEntry> completion = cq_.poll();
        if (!completion.has_value()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // NOTE: the command is still in flight so the cid should not be released
                return std::unexpected{Error{
                    .reason = std::format(
                        "Timed out after {}s waiting for CID {} on queue {}",
                        timeout.count(),
                        cid,
                        qid_.value
                    ),
                    .severity = Error::Severity::ERROR
                }};
            }

            std::this_thread::yield();  // TODO: __mm_pause()?
            continue;
        }

        if (auto diag = write_complete(*completion)) {
            return std::unexpected{std::move(*diag)};
        }
    }
}

std::optional<Result<wire::CompletionEntry>> Queue::poll(Cid cid) {
    if (!requests_.pending(cid)) {
        return std::unexpected{not_in_flight(cid)};
    }

    while (const auto completion = cq_.poll()) {
        if (auto diag = write_complete(*completion)) {
            return std::unexpected{std::move(*diag)};
        }
    }

    return requests_.take(cid);
}

void Queue::abandon(Cid cid) {
    requests_.abandon(cid);
}

std::optional<Error> Queue::write_complete(const wire::CompletionEntry& completion) {
    sq_.advance_head(completion.sq_head);
    return requests_.complete(completion);
}

}  // namespace nvme
