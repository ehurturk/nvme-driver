#pragma once

#include <cstdint>
#include <assert.h>

#include "../utils.hpp"
#include "../queue_entry.hpp"
#include "command_layout.hpp"
#include "features.hpp"

namespace nvme::admin
{

enum class Opcode : std::uint8_t {
    DeleteIoSq = 0x00,
    CreateIoSq = 0x01,
    GetLogPage = 0x02,
    DeleteIoCq = 0x04,
    CreateIoCq = 0x05,
    Identify = 0x06,
    Abort = 0x08,
    SetFeatures = 0x09,
    GetFeatures = 0x0A,
    AsyncEvent = 0x0C,
};

namespace commands
{

// abort (0x08)
// completion dw0 bit 0: 1 = not aborted, 0 = abort was not applied
struct Abort {
    std::uint16_t sqid{};
    std::uint16_t cid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::Abort, this_cid);
        c.cdw10 = layout::abort_cdw10::encode(sqid, cid);
        return c;
    }

    struct Completion {
        bool immediate_abort_not_performed;
    };

    static Completion decode_completion(const CompletionEntry& c)
    {
        return Completion{
                .immediate_abort_not_performed = (c.completion_result.cdw0 & 0x1) == 1};
    }
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
struct CreateIoCq {
    std::uint64_t prp1{};
    std::uint16_t qid{};
    std::uint16_t qsize{};
    std::uint16_t interrupt_vector = 0;
    bool interrupts_enabled = false;
    bool physically_contiguous = true;

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::CreateIoCq, this_cid);

        c.dptr = DataPointer::prp(prp1);
        c.cdw10 = layout::create_io_q_cdw10::encode(qid, qsize);
        c.cdw11 = layout::create_io_cq_cdw11::encode(
                physically_contiguous, interrupts_enabled, interrupt_vector);

        return c;
    }
    struct Completion {};
    static Completion decode_completion(const CompletionEntry& c) { return Completion{}; }
};

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
struct CreateIoSq {
    std::uint64_t prp1{};
    std::uint16_t qid{};
    std::uint16_t qsize{};
    std::uint16_t cqid{};
    layout::QueuePriority priority = layout::QueuePriority::Urgent;
    bool physically_contiguous = true;
    std::uint16_t nvmsetid = 0;

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::CreateIoSq, this_cid);

        c.dptr = DataPointer::prp(prp1);
        c.cdw10 = layout::create_io_q_cdw10::encode(qid, qsize);
        c.cdw11 = layout::create_io_sq_cdw11::encode(
                physically_contiguous, priority, cqid);
        c.cdw12 = layout::create_io_sq_cdw12::encode(nvmsetid);

        return c;
    }
    struct Completion {};
    static Completion decode_completion(const CompletionEntry& c) { return Completion{}; }
};

// The delete IO CQ command is used to delete an IO completion queue.
// It uses the Command DWORD 10 field.
// NOTE: - After this command has been completed, the PRP List that describes
//         the CQ may be deallocated by the host.
//       - Host shall ensure that any associated I/O Submission Queue is deleted
//         deleted prior to deleting a CQ.
struct DeleteIoCq {
    std::uint16_t qid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::DeleteIoCq, this_cid);
        c.cdw10 = layout::delete_io_q_cdw10::encode(qid);
        return c;
    }

    struct Completion {};
    static Completion decode_completion(const CompletionEntry& c) { return Completion{}; }

};

// The delete IO SQ command is used to delete an IO submission queue.
// It uses the Command DWORD 10 field.
// NOTE: - After this command has been completed, the PRP List that describes
//         the SQ may be deallocated by the host.
struct DeleteIoSq {
    std::uint16_t qid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::DeleteIoSq, this_cid);
        c.cdw10 = layout::delete_io_q_cdw10::encode(qid);
        return c;
    }
    struct Completion {};
    static Completion decode_completion(const CompletionEntry& c) { return Completion{}; }
};

// Identify (opcode 0x06)
// DPTR (128 bits): start of the data buffer
// CDW10: bits 0 - 7 CNS: Controller or namespace structure
//            16 - 31 CNTID: Controller Identifier
// CDW11: bits 0 - 15: CNSSID (CNS Specific Identifier): Dependent on the specified CNS value
//            24 - 31: CSI: Command Set Identifier (if not used should be cleared to 0x0).
struct Identify {
    // controller namespace
    enum class Cns : std::uint8_t {
        Namespace = 0x00,
        Controller = 0x01,
        ActiveNamespaceIds = 0x02,
    };

    std::uint64_t prp1{}; // iova of 4 KiB result buffer
    Cns cns{};
    std::uint32_t nsid = 0; // only for namespace scoped cns values
    std::uint16_t cntid = 0;
    std::uint16_t cnssid = 0;
    std::uint8_t csi = 0;

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::Identify, this_cid);

        c.nsid = nsid;
        c.dptr = DataPointer::prp(prp1);
        c.cdw10 = layout::identify_cdw10::encode(
                utils::to_underlying(cns), cntid);
        c.cdw11 = layout::identify_cdw11::encode(cnssid, csi);

        return c;
    }

    struct Completion {
    };

    static Completion decode_completion(const CompletionEntry& c) { return Completion{}; }
};

struct AsyncEventRequest {
    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        return CommonCommand::build(Opcode::AsyncEvent, this_cid);
    }

    enum class AsynchronousEventType : std::uint8_t {
        ErrorStatus = 0b000,
        SMART_HealthStatus = 0b001,
        Notice = 0b010,
        Immediate = 0b011,
        OneShot = 0b100,
        IOCommandSpecificStatus = 0b110
    };

    struct Completion {
        using AET = utils::BitField<std::uint32_t, 0, 3>;
        using AEI = utils::BitField<std::uint32_t, 8, 8>;
        using LID = utils::BitField<std::uint32_t, 16, 8>;

        // cdw0
        AsynchronousEventType type;
        std::uint8_t information;
        std::uint8_t log_page_id;

        // cdw1
        DWRD event_specific_param;
    };

    static Completion decode_completion(const CompletionEntry& c)
    {
        Completion cl{};
        DWRD dw0 = c.completion_result.cdw0;
        cl.type = static_cast<AsynchronousEventType>(Completion::AET::extract(dw0));
        cl.information = Completion::AEI::extract(dw0);
        cl.log_page_id = Completion::LID::extract(dw0);
        cl.event_specific_param = c.completion_result.cdw1;

        return cl;
    }
};

// SetFeatures (0x09)
// CDW10: bits 0 - 7: Feature Identifier (FID)
//               31 : Save (SV): specifies that the controller shall save the attribute so that the attribute persists
//                               through all power states and resets.
// For feature NumberOfQueues (0x07):
//   CDW11: bits 10 - 15: Number of I/O Sub Queues Requested (NSQR): # of IO queues requested (not including the admin queue).
//                                                                   is 0s based
//               16 - 31: Number of I/O Compl. Queues Requested (NCQR), is 0s based
// For feature InterruptCoalescing (0x08):
//   CDW11: bits 0 - 7: Aggregation Threshold (THR): Specifies the recommended minimum number of completion queue
//                                                   entries to aggregate per interrupt vector before signaling an
//                                                   interrupt to the host. Is a 0's based value
//          bits 8 - 15: Aggregation Time (TIME): Specifies the recommended maximum time in 100 microsecond increments
//                                                that a controller may delay an interrupt due to interrupt coalescing.
// CDW14: bits 0 - 6: UUID index
template <typename Feature> struct SetFeatures {
    Feature feature;
    bool save = false;
    std::uint64_t prp1 = 0;
    std::uint8_t uuid_index = 0;

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::SetFeatures, this_cid);

        c.dptr = DataPointer::prp(prp1);
        c.cdw10 = set_features_cdw10(Feature::id, save);
        c.cdw11 = feature.encode_cdw11();
        c.cdw14 = features::cdw14_uuid_index(uuid_index);

        return c;
    }

    using Completion = typename Feature::Completion;

    static Completion decode_completion(const CompletionEntry &e)
    {
        return Feature::decode_completion(e);
    }
};

template <typename Feature> struct GetFeatures {
    features::Select sel = features::Select::Current;
    std::uint64_t prp1 = 0;
    std::uint8_t uuid_index = 0;

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::GetFeatures, this_cid);

        c.dptr = DataPointer::prp(prp1);
        c.cdw10 = get_features_cdw10(Feature::id, sel);
        c.cdw14 = features::cdw14_uuid_index(uuid_index);

        return c;
    }

    using Completion = typename Feature::Completion;

    static Completion decode_completion(const CompletionEntry &e)
    {
        return Feature::decode_completion(e);
    }
};

} // namespace commands

} // namespace nvme::admin
