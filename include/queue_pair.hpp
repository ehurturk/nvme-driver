#pragma once

#include <vector>

#include "submission_queue.hpp"
#include "completion_queue.hpp"
#include "traits.hpp"
#include "dma_memory.hpp"

namespace nvme
{

// NOTE: "Typical NVMe implementation attempts to allocate
//        a maximum of 1 queue pair per host CPU core."
//        The Admin Submission Queue and Admin Completion Queue are required to be in physically contiguous
//        memory.

class AdminQueuePair
{
  private:
        static constexpr size_t AQ_ID = 0x0;
        static constexpr size_t AQ_DEPTH = 32; // taken from linux
                                               // kernel nvme driver
  public:
    AdminQueuePair(void *bar0_base_addr, std::uint8_t dstrd)
            : memory_{0},
              sq_{memory_, create_hardware_context(bar0_base_addr, dstrd)},
              cq_{memory_, create_hardware_context(bar0_base_addr, dstrd)}
    {
    }

    ~AdminQueuePair();

    AdminQueuePair(const AdminQueuePair &) = delete;
    AdminQueuePair &operator=(const AdminQueuePair &) = delete;

    AdminQueuePair(AdminQueuePair &&) noexcept;
        AdminQueuePair &operator=(AdminQueuePair &&) noexcept;

        std::uint64_t sq_iova() const { return memory_.iova(); }
        std::uint64_t cq_iova() const { return memory_.iova() + sq_region_bytes(); }

private:
    static QueueHardwareContext create_hardware_context(
            void *bar0_base_addr, std::uint8_t dstrd)
    {
        QueueHardwareContext ctx{};
        ctx.queue_id = AQ_ID;
        auto *base = static_cast<std::byte *>(bar0_base_addr);
        ctx.sq_tail_dbell =
                reinterpret_cast<volatile std::uint32_t *>(base + 0x1000);
        ctx.cq_head_dbell = reinterpret_cast<volatile std::uint32_t *>(
                base + 0x1000 + (4u << dstrd));
        return ctx;
    }

    static constexpr std::size_t sq_bytes()  { return AQ_DEPTH * (1 << constants::ADM_SQES); }
    static constexpr std::size_t cq_bytes()  { return AQ_DEPTH * (1 << constants::ADM_CQES); }
    static constexpr std::size_t sq_region_bytes() { return page_round_up(sq_bytes()); }
    static constexpr std::size_t cq_region_bytes() { return page_round_up(cq_bytes()); }
    static constexpr std::size_t region_bytes()    { return sq_region_bytes() + cq_region_bytes(); }

  private:
    DmaMemory memory_;
    SubmissionQueue<AdminQueueKind> sq_;
    CompletionQueue<AdminQueueKind> cq_;
};

class IOQueuePair
{
  private:
    std::vector<SubmissionQueue<AdminQueueKind>> sqs_;
    CompletionQueue<AdminQueueKind> cq_;

    DmaMemory mem_;
};

} // namespace nvme
