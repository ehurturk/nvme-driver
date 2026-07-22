#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include "utils.hpp"

namespace nvme
{

namespace admin
{
enum class Opcode : std::uint8_t;
}

namespace sqe::cdw0
{

using OPC  = utils::BitField<DWRD, 0, 8>;
using FUSE = utils::BitField<DWRD, 8, 2>;
using PSDT = utils::BitField<DWRD, 14, 2>;
using CID  = utils::BitField<DWRD, 16, 16>;

enum class Psdt : std::uint8_t
{
    Prp  = 0b00,
    Sgl1 = 0b01,
    Sgl2 = 0b10,
};

inline DWRD encode(admin::Opcode opc, std::uint16_t cid, Psdt psdt = Psdt::Prp) noexcept
{
    return utils::DwordBuilder<>{}
            .set<OPC>(utils::to_underlying(opc))
            .set<PSDT>(utils::to_underlying(psdt))
            .set<CID>(cid)
            .get();
}
} // namespace sqe::cdw0

struct DataPointer
{
    std::uint64_t prp1;
    std::uint64_t prp2;

    static constexpr DataPointer prp(std::uint64_t p1, std::uint64_t p2 = 0) noexcept
    {
        return DataPointer{.prp1 = p1, .prp2 = p2};
    }
};

struct CommonCommand
{
    DWRD          cdw0;
    DWRD          nsid;
    DWRD          cdw2;
    DWRD          cdw3;
    std::uint64_t mptr;
    DataPointer   dptr;
    DWRD          cdw10;
    DWRD          cdw11;
    DWRD          cdw12;
    DWRD          cdw13;
    DWRD          cdw14;
    DWRD          cdw15;

    static CommonCommand build(admin::Opcode opc, std::uint16_t cid, sqe::cdw0::Psdt psdt = sqe::cdw0::Psdt::Prp)
    {
        CommonCommand c{};
        c.cdw0 = sqe::cdw0::encode(opc, cid, psdt);
        return c;
    }
};

namespace cqe
{
namespace status
{
using P   = utils::BitField<std::uint16_t, 0, 1>;  // phase tag
using SC  = utils::BitField<std::uint16_t, 1, 8>;  // status code
using SCT = utils::BitField<std::uint16_t, 9, 3>;  // status code type
using CRD = utils::BitField<std::uint16_t, 12, 2>; // command retry delay
using M   = utils::BitField<std::uint16_t, 14, 1>; // more
using DNR = utils::BitField<std::uint16_t, 15, 1>; // do not retry

enum class StatusCodeType : std::uint8_t
{
    Generic            = 0x0,
    CommandSpecific    = 0x1,
    MediaDataIntegrity = 0x2,
    PathRelated        = 0x3,
};

} // namespace status

} // namespace cqe

// Contents of Dword 0 and Dword 1 in a CompletionEntry are
// command specific. If a command uses Dword 0 or Dwod 1, then
// the definition of these dwords is contained with the associated
// command definition.
struct CompletionResult
{
    DWRD cdw0;
    DWRD cdw1;
};

// this is bits 17-31 of CDW3
struct CompletionStatus
{
    std::uint16_t status;

    inline bool succeeded() const
    {
        // SC bits are bits 1-8 and SCT Bits are bits 9-11
        // so this bitmask is bits 1-11:
        // 0x0ffe = 0000_1111_1111_1110
        return (status & 0x0ffe) == 0;
    }

    inline std::uint8_t status_code() const
    {
        return static_cast<std::uint8_t>(cqe::status::SC::extract(status));
    }

    inline cqe::status::StatusCodeType status_code_type() const
    {
        auto val = cqe::status::SCT::extract(status);
        return static_cast<cqe::status::StatusCodeType>(val);
    }

    inline std::uint8_t phase() const
    {
        return static_cast<std::uint8_t>(cqe::status::P::extract(status));
    }

    inline std::uint8_t command_retry_delay() const
    {
        return static_cast<std::uint8_t>(cqe::status::CRD::extract(status));
    }

    inline bool do_not_retry() const
    {
        return static_cast<bool>(cqe::status::DNR::extract(status));
    }

    inline bool more() const
    {
        return static_cast<bool>(cqe::status::M::extract(status));
    }
};

static_assert(sizeof(CompletionStatus) == sizeof(std::uint16_t));

struct CompletionEntry
{
    // cdw0 and cdw1:
    CompletionResult completion_result;

    // cdw2:
    std::uint16_t sq_head;
    std::uint16_t sqid;

    // cdw3:
    std::uint16_t cid;
    std::uint16_t status; // first bit is the present bit

    inline CompletionStatus stat() const
    {
        return CompletionStatus{.status = status};
    };
};

namespace sqe
{
// build CDW0 from named fields
inline DWRD make_cdw0(std::uint8_t opc, std::uint16_t cid, std::uint8_t fuse = 0, std::uint8_t psdt = 0)
{
    DWRD d = 0;
    d      = cdw0::OPC::insert(d, opc);
    d      = cdw0::FUSE::insert(d, fuse);
    d      = cdw0::PSDT::insert(d, psdt);
    d      = cdw0::CID::insert(d, cid);
    return d;
}

inline std::uint8_t opcode(const CommonCommand& c)
{
    return static_cast<std::uint8_t>(cdw0::OPC::extract(c.cdw0));
}
inline std::uint16_t cid(const CommonCommand& c)
{
    return static_cast<std::uint16_t>(cdw0::CID::extract(c.cdw0));
}
inline std::uint8_t psdt(const CommonCommand& c)
{
    return static_cast<std::uint8_t>(cdw0::PSDT::extract(c.cdw0));
}
inline std::uint8_t fuse(const CommonCommand& c)
{
    return static_cast<std::uint8_t>(cdw0::FUSE::extract(c.cdw0));
}

} // namespace sqe

// check props
static_assert(sizeof(DWRD) == 4, "DWRD must be 4 bytes in size");
static_assert(sizeof(DataPointer) == 16, "DataPointer must be 2 QWRD (16-bytes) in size");
static_assert(sizeof(CompletionResult) == 8, "CompletionResult must be 2 DWRD (8-bytes) in size");

static_assert(sizeof(CommonCommand) == 64, "SQE must be 64 bytes");
static_assert(sizeof(CompletionEntry) == 16, "CQE must be 16 bytes");

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

static_assert(offsetof(CompletionEntry, completion_result) == 0);
static_assert(offsetof(CompletionEntry, sq_head) == 8);
static_assert(offsetof(CompletionEntry, sqid) == 10);
static_assert(offsetof(CompletionEntry, cid) == 12);
static_assert(offsetof(CompletionEntry, status) == 14);

} // namespace nvme

namespace std
{
template <> struct formatter<nvme::CompletionStatus>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        auto it = ctx.begin();

        // Only "{}" is supported; reject custom specifiers such as "{:x}".
        if (it != ctx.end() && *it != '}')
            throw format_error("invalid format specifier for nvme::CompletionStatus");

        return it;
    }

    auto format(const nvme::CompletionStatus& c, format_context& ctx) const
    {
        return format_to(ctx.out(),
                         "raw=0x{:04x}, "
                         "succeeded={}, "
                         "phase={}, "
                         "sct={}, "
                         "sc={}, "
                         "crd={}, "
                         "more={}, "
                         "dnr={}",
                         c.status, c.succeeded(), static_cast<unsigned>(c.phase()),
                         utils::to_underlying(c.status_code_type()), static_cast<unsigned>(c.status_code()),
                         static_cast<unsigned>(c.command_retry_delay()), c.more(), c.do_not_retry());
    }
};
} // namespace std
