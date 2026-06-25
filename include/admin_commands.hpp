#pragma once

#include "utils.hpp"
#include "queue_entry.hpp"

#include <cstdint>

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
    using CID = utils::BitField<std::uint32_t, 0, 16>;
    using SQID = utils::BitField<std::uint32_t, 16, 16>;
    using NOT_APPLIED = utils::BitField<std::uint32_t, 0, 1>;

    std::uint16_t sqid;
    std::uint16_t cid;

    CommonCommand build(std::uint16_t this_cid) const
    {
        CommonCommand c{};
        c.cdw0 = sqe::make_cdw0(std::uint8_t(Opcode::Abort), this_cid);
        c.cdw10 = CID::insert(SQID::insert(0, sqid), cid);
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
    using QID = utils::BitField<std::uint32_t, 0, 16>;
    using QSIZE = utils::BitField<std::uint32_t, 16, 16>;
    using PC = utils::BitField<std::uint32_t, 0, 1>;
    using IEN = utils::BitField<std::uint32_t, 1, 1>;
    using IV = utils::BitField<std::uint32_t, 16, 16>;

    std::uint64_t prp1;
    std::uint16_t qid;
    std::uint16_t qsize;
    std::uint16_t iv = 0;
    bool interrupts_enabled = false;
    bool phys_contig = true; // must be true for a single contiguous buffer

    CommonCommand build(std::uint16_t this_cid) const
    {
        CommonCommand c{};
        c.cdw0 = sqe::make_cdw0(std::uint8_t(Opcode::CreateIoCq), this_cid);
        c.dptr.prp1 = prp1; // TODO: Check CDW11.PC and SGLs?
        c.cdw10 = QSIZE::insert(QID::insert(0, qid),
                std::uint16_t(qsize - 1)); // QSIZE is 0 based so sub 1
        std::uint32_t cdw11 = PC::insert(0, phys_contig ? 1 : 0);
        cdw11 = IEN::insert(cdw11, interrupts_enabled ? 1 : 0);
        cdw11 = IV::insert(cdw11, iv);
        c.cdw11 = cdw11;
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
    using QID = utils::BitField<std::uint32_t, 0, 16>;
    using QSIZE = utils::BitField<std::uint32_t, 16, 16>;
    using PC = utils::BitField<std::uint32_t, 0, 1>;
    using QPRIO = utils::BitField<std::uint32_t, 1, 2>;
    using CQID = utils::BitField<std::uint32_t, 16, 16>;

    std::uint64_t prp1;
    std::uint16_t qid;
    std::uint16_t qsize;
    std::uint16_t cqid;
    std::uint8_t qprio =
            0; // default to urgent TODO: check if Weighted round robin is used?
    bool phys_contig = true;

    CommonCommand encode(std::uint16_t this_cid) const
    {
        CommonCommand c{};
        c.cdw0 = sqe::make_cdw0(std::uint8_t(Opcode::CreateIoSq), this_cid);
        c.dptr.prp1 = prp1;
        c.cdw10 = QSIZE::insert(QID::insert(0, qid), std::uint16_t(qsize - 1));
        std::uint32_t d11 = PC::insert(0, phys_contig ? 1 : 0);
        d11 = QPRIO::insert(d11, qprio);
        d11 = CQID::insert(d11, cqid);
        c.cdw11 = d11;
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
    using CNS = utils::BitField<std::uint32_t, 0, 8>;
    using CNTID = utils::BitField<std::uint32_t, 16, 16>;

    enum class Cns : std::uint8_t {
        Namespace = 0x00, // requires nsid
        Controller = 0x01,
        ActiveNamespaceIds = 0x02,
    };

    std::uint64_t prp1; // IOVA of 4 KiB result buffer
    Cns cns;
    std::uint32_t nsid = 0; // only for namespace scoped CNS values
    std::uint16_t cntid = 0;

    CommonCommand encode(std::uint16_t this_cid) const
    {
        CommonCommand c{};
        c.cdw0 = sqe::make_cdw0(std::uint8_t(Opcode::Identify), this_cid);
        c.nsid = nsid;
        c.dptr.prp1 = prp1;
        c.cdw10 = CNTID::insert(
                CNS::insert(0, std::uint8_t(cns)), cntid);
        return c;
    }
};

} // namespace commands

} // namespace nvme::admin

// TODO: add entries for the command completion values asw
