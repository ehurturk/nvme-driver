#pragma once

#include "queue_pair.hpp"
#include "traits.hpp"
#include "utils.hpp"

namespace nvme
{

using DWRD = std::uint32_t;

namespace cdw0
{
using OPC = utils::BitField<DWRD, 0, 8>;
using FUSE = utils::BitField<DWRD, 8, 2>;
using PSDT = utils::BitField<DWRD, 14, 2>;
using CID = utils::BitField<DWRD, 16, 16>;
} // namespace CDW0

struct CDW0 {
    DWRD raw{};

    void set_opcode(std::uint8_t opc)
    {
        raw = cdw0::OPC::insert(raw, opc);
    }

    void set_cid(std::uint16_t cid)
    {
        raw = cdw0::CID::insert(raw, cid);
    }

    void set_psdt(std::uint8_t psdt)
    {
        raw = cdw0::PSDT::insert(raw, psdt);
    }

    void set_fuse(std::uint8_t fuse)
    {
        raw = cdw0::FUSE::insert(raw, fuse);
    }

    DWRD value() const { return raw; }
};

struct DataPointer {
    std::uint64_t prp1;
    std::uint64_t prp2;
};

struct CommonCommand {
    CDW0 cdw0;
    std::uint32_t nsid;
    DWRD cdw2;
    DWRD cdw3;
    std::uint64_t mptr;
    DataPointer dptr;
    DWRD cdw10;
    DWRD cdw11;
    DWRD cdw12;
    DWRD cdw13;
    DWRD cdw14;
    DWRD cdw15;
};

static_assert(sizeof(DWRD) == 4);
static_assert(sizeof(CommonCommand) == 64);
static_assert(std::is_standard_layout_v<CommonCommand>);
static_assert(std::is_trivially_copyable_v<CommonCommand>);

static_assert(offsetof(CommonCommand, cdw0)  == 0);
static_assert(offsetof(CommonCommand, nsid)  == 4);
static_assert(offsetof(CommonCommand, cdw2)  == 8);
static_assert(offsetof(CommonCommand, cdw3)  == 12);
static_assert(offsetof(CommonCommand, mptr)  == 16);
static_assert(offsetof(CommonCommand, dptr)  == 24);
static_assert(offsetof(CommonCommand, cdw10) == 40);
static_assert(offsetof(CommonCommand, cdw11) == 44);
static_assert(offsetof(CommonCommand, cdw12) == 48);
static_assert(offsetof(CommonCommand, cdw13) == 52);
static_assert(offsetof(CommonCommand, cdw14) == 56);
static_assert(offsetof(CommonCommand, cdw15) == 60);

struct AdminSubmissionEntry {
};

struct AdminCompletionEntry {
};

using AdminQueuePair = QueuePair<AdminQueueKind>;

} // namespace nvme
