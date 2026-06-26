#pragma once

#include "utils.hpp"
#include "queue_entry.hpp"

#include <cstdint>
#include <assert.h>

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

namespace layout
{

namespace abort_cdw10
{
using SQID = utils::BitField<DWRD, 0, 16>;
using CID  = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(std::uint16_t sqid, std::uint16_t cid) noexcept
{
    return utils::DwordBuilder<>{}
        .set<SQID>(sqid)
        .set<CID>(cid)
        .get();
}
} // namespace abort_cdw10

namespace create_io_q_cdw10
{
using QID   = utils::BitField<DWRD, 0, 16>;
using QSIZE = utils::BitField<DWRD, 16, 16>;

// NOTE: Qsize should be 1s based
inline DWRD encode(std::uint16_t qid, std::uint16_t qsize) noexcept
{
    assert(qsize > 0);

    return utils::DwordBuilder<>{}
        .set<QID>(qid)
        .set<QSIZE>(qsize - 1)
        .get();
}
} // namespace create_io_q_cdw10

namespace create_io_cq_cdw11
{
using PC  = utils::BitField<DWRD, 0, 1>;
using IEN = utils::BitField<DWRD, 1, 1>;
using IV  = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(bool physically_contiguous,
                    bool interrupts_enabled,
                    std::uint16_t interrupt_vector) noexcept
{
    return utils::DwordBuilder<>{}
        .set<PC>(physically_contiguous)
        .set<IEN>(interrupts_enabled)
        .set<IV>(interrupt_vector)
        .get();
}
} // namespace create_io_cq_cdw11

enum class QueuePriority : std::uint8_t {
    Urgent = 0b00,
    High   = 0b01,
    Medium = 0b10,
    Low    = 0b11,
};

namespace create_io_sq_cdw11
{
using PC    = utils::BitField<DWRD, 0, 1>;
using QPRIO = utils::BitField<DWRD, 1, 2>;
using CQID  = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(bool physically_contiguous,
                    QueuePriority priority,
                    std::uint16_t cqid) noexcept
{
    return utils::DwordBuilder<>{}
        .set<PC>(physically_contiguous)
        .set<QPRIO>(utils::to_underlying(priority))
        .set<CQID>(cqid)
        .get();
}
} // namespace create_io_sq_cdw11

namespace create_io_sq_cdw12
{
using NVMSETID = utils::BitField<DWRD, 0, 16>;

inline DWRD encode(std::uint16_t nvmsetid) noexcept
{
    return utils::DwordBuilder<>{}
        .set<NVMSETID>(nvmsetid)
        .get();
}
} // namespace create_io_sq_cdw12

namespace identify_cdw10
{
using CNS   = utils::BitField<DWRD, 0, 8>;
using CNTID = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(std::uint8_t cns, std::uint16_t cntid) noexcept
{
    return utils::DwordBuilder<>{}
        .set<CNS>(cns)
        .set<CNTID>(cntid)
        .get();
}
} // namespace identify_cdw10

namespace identify_cdw11
{
using CNSSID = utils::BitField<DWRD, 0, 16>;
using CSI    = utils::BitField<DWRD, 24, 8>;

inline DWRD encode(std::uint16_t cnssid, std::uint8_t csi) noexcept
{
    return utils::DwordBuilder<>{}
        .set<CNSSID>(cnssid)
        .set<CSI>(csi)
        .get();
}
} // namespace identify_cdw11

namespace delete_io_q_cdw10
{
using QID = utils::BitField<DWRD, 0, 16>;

inline DWRD encode(std::uint16_t qid) noexcept
{
    return utils::DwordBuilder<>{}
        .set<QID>(qid)
        .get();
}
} // namespace delete_io_q_cdw10

} // namespace layout

namespace features
{

// Bit fields:
using FID  = utils::BitField<DWRD, 0, 8>;
using SEL  = utils::BitField<DWRD, 8, 3>;
using SV = utils::BitField<DWRD, 31, 1>;

// CDW14:
using UIDX = utils::BitField<DWRD, 0, 7>;

enum class FeatureId : std::uint8_t {
    NumberOfQueues = 0x07,
    InterruptCoalescing = 0x08,
};

enum class Select : std::uint8_t {
    Current = 0b000,
    Default = 0b001,
    Saved = 0b010,
    SupportedCapability = 0b011,
};

inline DWRD set_features_cdw10(FeatureId fid, bool save) noexcept
{
    return utils::DwordBuilder<>{}
        .set<FID>(utils::to_underlying(fid))
        .set<SV>(save)
        .get();
}

inline DWRD get_features_cdw10(FeatureId fid, Select sel) noexcept
{
    return utils::DwordBuilder<>{}
        .set<FID>(utils::to_underlying(fid))
        .set<SEL>(utils::to_underlying(sel))
        .get();
}

inline DWRD cdw14_uuid_index(std::uint8_t uuid_index) noexcept
{
    return utils::DwordBuilder<>{}
        .set<UIDX>(uuid_index)
        .get();
}

// Number of Queues: Function Id: 0x07
struct NumberOfQueues {
    static constexpr FeatureId id = FeatureId::NumberOfQueues;

    // number of submission & completion queues.
    // Write these as 1s based, it is converted to 0s based on encoding.
    std::uint16_t nsq{};
    std::uint16_t ncq{};

    struct Cdw11 {
        using NSQR = utils::BitField<DWRD, 0, 16>; // 0 based
        using NCQR = utils::BitField<DWRD, 16, 16>; // 0 based
    };

    DWRD encode_cdw11() const noexcept
    {
        assert(nsq > 0);
        assert(ncq > 0);

        return utils::DwordBuilder<>{}
            .set<Cdw11::NSQR>(nsq - 1)
            .set<Cdw11::NCQR>(ncq - 1)
            .get();
    }

    // completion dw0
    struct Completion {
        using NSQA = utils::BitField<DWRD, 0, 16>;
        using NCQA = utils::BitField<DWRD, 16, 16>;

        static std::uint16_t allocated_sq(DWRD completion_dw0) noexcept
        {
            return static_cast<std::uint16_t>(NSQA::extract(completion_dw0) + 1);
        }

        static std::uint16_t allocated_cq(DWRD completion_dw0) noexcept
        {
            return static_cast<std::uint16_t>(NCQA::extract(completion_dw0) + 1);
        }
    };
};

// Interrupt Coalescing: Feature Id: 0x08
struct InterruptCoalescing {
    static constexpr FeatureId id = FeatureId::InterruptCoalescing;

    std::uint8_t threshold{};
    std::uint8_t time_100us{};

    struct Cdw11 {
        using THR  = utils::BitField<DWRD, 0, 8>;
        using TIME = utils::BitField<DWRD, 8, 8>;
    };

    DWRD encode_cdw11() const noexcept
    {
        return utils::DwordBuilder<>{}
            .set<Cdw11::THR>(threshold)
            .set<Cdw11::TIME>(time_100us)
            .get();
    }
};
} // namespace features

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
            physically_contiguous,
            interrupts_enabled,
            interrupt_vector
        );

        return c;
    }
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
            physically_contiguous,
            priority,
            cqid
        );
        c.cdw12 = layout::create_io_sq_cdw12::encode(nvmsetid);

        return c;
    }
};

struct DeleteIoCq {
    std::uint16_t qid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::DeleteIoCq, this_cid);
        c.cdw10 = layout::delete_io_q_cdw10::encode(qid);
        return c;
    }
};

struct DeleteIoSq {
    std::uint16_t qid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        CommonCommand c = CommonCommand::build(Opcode::DeleteIoSq, this_cid);
        c.cdw10 = layout::delete_io_q_cdw10::encode(qid);
        return c;
    }
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
            utils::to_underlying(cns),
            cntid
        );
        c.cdw11 = layout::identify_cdw11::encode(cnssid, csi);

        return c;
    }
};

struct AsyncEventRequest {
    CommonCommand build(std::uint16_t this_cid) const noexcept
    {
        return CommonCommand::build(Opcode::AsyncEvent, this_cid);
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
template <typename Feature>
struct SetFeatures {
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
};

template <typename Feature>
struct GetFeatures {
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
};

} // namespace commands

} // namespace nvme::admin
