#pragma once

#include "dma/buffer.hpp"
#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>

namespace nvme::admin::commands {

enum class QueuePriority : std::uint8_t {
    Urgent = 0b00,
    High = 0b01,
    Medium = 0b10,
    Low = 0b11,
};

// create I/O COmpletion Queue (0x05)
// prp entry 1 -> if CDW11.PC is set to 1, then this specifies a 64-bit base memory address pointer
//                of the COmpletion Queue that is physicall contiguous (memory page aligned).
//                if CDW11.PC is set to 0, then this specifies a PRP list pointer that describes
//                the list of pages that constitute the CQ.
// CDW10 -> bits 0 - 15: QID
//              16 - 31: QSIZE (not 0-based, this is subtracted in encode())
// CDW11 -> bit 0: PC. If set to 1, CQ is phys. contiguous and PRP1 is the addr of the contiguous
//                 physical buffer. If set to 0, then PRP1 is a PRP List pointer.
//              1: IEN: If set to 1, interrupts are enabled for this CQ. If is 0, interrupts
//                 are disabled.
//              2 - 15: Reserved
//              16 - 31: Interrupt Vector (IV): Transport specific (if PCIe it is MSI-X i think?)
class CreateIoCq {
public:
    static constexpr std::string_view name = "Create IO CQ";

    CreateIoCq(
        dma::DmaView queue_buf,
        QueueId qid,
        std::uint16_t qsize,
        bool interrupts_enabled = false,
        std::uint16_t interrupt_vector = 0,
        bool physically_contiguous = true
    )
        : prp1{queue_buf.iova},
          qid{qid.value},
          qsize{qsize},
          interrupt_vector{interrupt_vector},
          interrupts_enabled{interrupts_enabled},
          physically_contiguous{physically_contiguous} {
        assert(queue_buf.size >= std::size_t{qsize} * sizeof(wire::CompletionEntry));
    }

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept;

    struct Completion {
        std::uint16_t status;
    };

    static Completion decode_completion(const wire::CompletionEntry& c);

private:
    using QID = utils::BitField<DWRD, 0, 16>;
    // NOTE: Qsize should be 1s based
    using QSIZE = utils::BitField<DWRD, 16, 16>;
    using PC = utils::BitField<DWRD, 0, 1>;
    using IEN = utils::BitField<DWRD, 1, 1>;
    using IV = utils::BitField<DWRD, 16, 16>;

    std::uint64_t prp1;
    std::uint16_t qid;
    std::uint16_t qsize;
    std::uint16_t interrupt_vector;
    bool interrupts_enabled;
    bool physically_contiguous;
};

static_assert(CommandDescriptor<CreateIoCq>);

// create I/O Submission Queue (0x01)
// prp entry 1 -> If CDW11.PC Is 1 then this is the 64-bit base memory address pointer of the SQ
//                that is physicall contiguous.
//                If CDW11.PC is 0 then this is the PRP List pointer.
// CDW10 -> bits 0 - 15: QID
//              16 - 31: QSIZE (not 0-based, this is subtracted in encode())
// CDW11 -> bit 0: PC. If set to 1, CQ is phys. contiguous and PRP1 is the addr of the contiguous
//                 physical buffer. If set to 0, then PRP1 is a PRP List pointer.
//          1 - 2: Queue Priority (QPRIO): Specifies the priority class to use for commands within
//                 this SQ. This is only used when the WRRUP ARM is selected.
//                         0b00 -> urgent
//                         0b01 -> high
//                         0b10 -> medium
//                         0b11 -> low
//          3 - 15: Reserved
//         16 - 31: CQID: Specifies the identifier of the IO CQ to utilize for any command
//                  completions entries associated with this SQ.
//                  If this is:
//                    - 0x0 (Admin CQ), then the controller shall abort the command
//                    - outside the range, then the controller shall abort the command
//                    - does not specify IO CQ that has been created, the controller shall abort
//
// CDW12 -> bits 0 - 15: NVM Set Identifier (NVMSETID): Indicates the identifier of the NVM Set
//                       to be associated with this SQ.
//                       If this is 0x0 then this SQ is not associated with any NVM Set
class CreateIoSq {
public:
    static constexpr std::string_view name = "Create IO SQ";

    CreateIoSq(
        dma::DmaView queue_buf,
        QueueId qid,
        CompletionQueueId cqid,
        std::uint16_t qsize,
        QueuePriority priority = QueuePriority::Urgent,
        bool physically_contiguous = true,
        std::uint16_t nvmsetid = 0
    )
        : prp1{queue_buf.iova},
          qid{qid.value},
          qsize{qsize},
          cqid{cqid.value},
          priority{priority},
          physically_contiguous{physically_contiguous},
          nvmsetid{nvmsetid} {
        assert(queue_buf.size >= std::size_t{qsize} * sizeof(wire::SubmissionEntry));
    }

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept;

    struct Completion {};

    static Completion decode_completion(const wire::CompletionEntry&);

private:
    using QID = utils::BitField<DWRD, 0, 16>;
    // NOTE: Qsize should be 1s based
    using QSIZE = utils::BitField<DWRD, 16, 16>;
    using PC = utils::BitField<DWRD, 0, 1>;
    using QPRIO = utils::BitField<DWRD, 1, 2>;
    using CQID = utils::BitField<DWRD, 16, 16>;
    using NVMSETID = utils::BitField<DWRD, 0, 16>;

    std::uint64_t prp1;
    std::uint16_t qid;
    std::uint16_t qsize;
    std::uint16_t cqid;
    QueuePriority priority;
    bool physically_contiguous;
    std::uint16_t nvmsetid;
};

static_assert(CommandDescriptor<CreateIoSq>);

}  // namespace nvme::admin::commands
