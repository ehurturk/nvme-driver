#pragma once

#include "traits.hpp"
#include "utils.hpp"

namespace nvme
{

using DWRD = std::uint32_t;

namespace sqe
{
namespace cdw0
{
using OPC = utils::BitField<DWRD, 0, 8>;
using FUSE = utils::BitField<DWRD, 8, 2>;
using PSDT = utils::BitField<DWRD, 14, 2>;
using CID = utils::BitField<DWRD, 16, 16>;
} // namespace cdw0

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

    DWRD value() const
    {
        return raw;
    }
};

} // namespace sqe

struct DataPointer {
    std::uint64_t prp1;
    std::uint64_t prp2;
};

struct CommonCommand {
    sqe::CDW0 cdw0;
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

namespace cqe
{
namespace cdw2
{
using SQHP = utils::BitField<DWRD, 0, 16>;
using SQID = utils::BitField<DWRD, 16, 16>;
} // namespace cdw2

namespace cdw3
{
using CID = utils::BitField<DWRD, 0, 16>;
using P = utils::BitField<DWRD, 16, 1>;
using STATUS = utils::BitField<DWRD, 17, 15>;
} // namespace cdw3

struct CDW2 {
    DWRD raw{};

    void set_sqhp(std::uint16_t sqhp)
    {
        raw = cdw2::SQHP::insert(raw, sqhp);
    }

    void set_cid(std::uint16_t sqid)
    {
        raw = cdw2::SQID::insert(raw, sqid);
    }

    DWRD value() const
    {
        return raw;
    }
};

struct CDW3 {
    DWRD raw{};

    void set_cid(std::uint16_t cid)
    {
        raw = cdw3::CID::insert(raw, cid);
    }

    void set_phase(std::uint8_t p)
    {
        raw = cdw3::P::insert(raw, p);
    }
    void set_status(std::uint16_t status)
    {
        raw = cdw3::STATUS::insert(raw, status);
    }

    DWRD value() const
    {
        return raw;
    }
};

} // namespace cqe

struct CompletionEntry {
    DWRD cdw0;
    DWRD cdw1;
    cqe::CDW2 cdw2;
    cqe::CDW3 cdw3;
};

// check props
static_assert(sizeof(DWRD) == 4);
static_assert(sizeof(CommonCommand) == 64);

static_assert(std::is_standard_layout_v<CommonCommand>);
static_assert(std::is_trivially_copyable_v<CommonCommand>);

static_assert(std::is_standard_layout_v<CompletionEntry>);
static_assert(std::is_trivially_copyable_v<CompletionEntry>);

static_assert(offsetof(CommonCommand, cdw0) == 0);
static_assert(offsetof(CommonCommand, nsid) == 4);
static_assert(offsetof(CommonCommand, cdw2) == 8);
static_assert(offsetof(CommonCommand, cdw3) == 12);
static_assert(offsetof(CommonCommand, mptr) == 16);
static_assert(offsetof(CommonCommand, dptr) == 24);
static_assert(offsetof(CommonCommand, cdw10) == 40);
static_assert(offsetof(CommonCommand, cdw11) == 44);
static_assert(offsetof(CommonCommand, cdw12) == 48);
static_assert(offsetof(CommonCommand, cdw13) == 52);
static_assert(offsetof(CommonCommand, cdw14) == 56);
static_assert(offsetof(CommonCommand, cdw15) == 60);

static_assert(sizeof(CompletionEntry) == 16, "CQE must be 16 bytes");
} // namespace nvme
