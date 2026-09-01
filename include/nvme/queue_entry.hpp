#pragma once

#include "utils.hpp"
#include "nvme/admin/opcodes.hpp"
#include "nvme/io/opcodes.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <type_traits>

namespace nvme {

namespace admin {
enum class Opcode : std::uint8_t;
}  // namespace admin

namespace wire {

struct DataPointer {
    std::uint64_t prp1;
    std::uint64_t prp2;

    static constexpr DataPointer prp(std::uint64_t p1, std::uint64_t p2 = 0) noexcept {
        return DataPointer{.prp1 = p1, .prp2 = p2};
    }
};

template <typename Opc>
concept Opcode = std::same_as<Opc, nvme::admin::Opcode> || std::same_as<Opc, nvme::io::Opcode>;

struct SubmissionEntry {
    enum class Psdt : std::uint8_t {
        Prp = 0b00,
        Sgl1 = 0b01,
        Sgl2 = 0b10,
    };

    DWRD cdw0;
    DWRD nsid;
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

    template <Opcode Opc>
    static SubmissionEntry build(Opc opc, std::uint16_t cid, Psdt psdt = Psdt::Prp);

    std::uint8_t opcode() const;

    std::uint16_t cid() const;

    std::uint8_t psdt() const;

    std::uint8_t fuse() const;

private:
    using OPC = utils::BitField<DWRD, 0, 8>;
    using FUSE = utils::BitField<DWRD, 8, 2>;
    using PSDT = utils::BitField<DWRD, 14, 2>;
    using CID = utils::BitField<DWRD, 16, 16>;
};

// this is bits 17-31 of CDW3
struct CompletionStatus {
    enum class StatusCodeType : std::uint8_t {
        Generic = 0x0,
        CommandSpecific = 0x1,
        MediaDataIntegrity = 0x2,
        PathRelated = 0x3,
    };

    std::uint16_t status;

    bool succeeded() const;

    std::uint8_t status_code() const;

    StatusCodeType status_code_type() const;

    std::uint8_t phase() const;

    std::uint8_t command_retry_delay() const;

    bool do_not_retry() const;

    bool more() const;

    operator int() const noexcept;

private:
    using P = utils::BitField<std::uint16_t, 0, 1>;     // phase tag
    using SC = utils::BitField<std::uint16_t, 1, 8>;    // status code
    using SCT = utils::BitField<std::uint16_t, 9, 3>;   // status code type
    using CRD = utils::BitField<std::uint16_t, 12, 2>;  // command retry delay
    using M = utils::BitField<std::uint16_t, 14, 1>;    // more
    using DNR = utils::BitField<std::uint16_t, 15, 1>;  // do not retry
};

struct CompletionEntry {
    // Contents of Dword 0 and Dword 1 in a CompletionEntry are
    // command specific. If a command uses Dword 0 or Dwod 1, then
    // the definition of these dwords is contained with the associated
    // command definition.
    struct {
        DWRD cdw0;
        DWRD cdw1;
    } completion_result;

    // cdw2:
    std::uint16_t sq_head;
    std::uint16_t sqid;

    // cdw3:
    std::uint16_t cid;
    CompletionStatus status;  // first bit is the present bit

    bool ok() const;
};

// check props
static_assert(sizeof(DWRD) == 4, "DWRD must be 4 bytes in size");
static_assert(sizeof(DataPointer) == 16, "DataPointer must be 2 QWRD (16-bytes) in size");

static_assert(
    sizeof(CompletionStatus) == sizeof(std::uint16_t),
    "CompletionStatus should be 16-bytes in size"
);

static_assert(sizeof(SubmissionEntry) == 64, "SQE must be 64 bytes");
static_assert(sizeof(CompletionEntry) == 16, "CQE must be 16 bytes");

static_assert(std::is_standard_layout_v<SubmissionEntry>);
static_assert(std::is_trivially_copyable_v<SubmissionEntry>);

static_assert(std::is_standard_layout_v<CompletionEntry>);
static_assert(std::is_trivially_copyable_v<CompletionEntry>);

static_assert(offsetof(SubmissionEntry, cdw0) == 0);
static_assert(offsetof(SubmissionEntry, nsid) == 4);
static_assert(offsetof(SubmissionEntry, cdw2) == 8);
static_assert(offsetof(SubmissionEntry, cdw3) == 12);
static_assert(offsetof(SubmissionEntry, mptr) == 16);
static_assert(offsetof(SubmissionEntry, dptr) == 24);
static_assert(offsetof(SubmissionEntry, cdw10) == 40);
static_assert(offsetof(SubmissionEntry, cdw11) == 44);
static_assert(offsetof(SubmissionEntry, cdw12) == 48);
static_assert(offsetof(SubmissionEntry, cdw13) == 52);
static_assert(offsetof(SubmissionEntry, cdw14) == 56);
static_assert(offsetof(SubmissionEntry, cdw15) == 60);

static_assert(offsetof(CompletionEntry, completion_result) == 0);
static_assert(offsetof(CompletionEntry, sq_head) == 8);
static_assert(offsetof(CompletionEntry, sqid) == 10);
static_assert(offsetof(CompletionEntry, cid) == 12);
static_assert(offsetof(CompletionEntry, status) == 14);

}  // namespace wire

}  // namespace nvme

namespace std {
template<>
struct formatter<nvme::wire::CompletionStatus> {
    static constexpr auto parse(format_parse_context& ctx) {
        const auto *it = ctx.begin();
        
        if (it != ctx.end() && *it != '}') {
            throw format_error("invalid format specifier for nvme::wire::CompletionStatus");
        }

        return it;
    }

    static auto format(const nvme::wire::CompletionStatus& c, format_context& ctx)  {
        return format_to(
            ctx.out(),
            "raw=0x{:04x}, "
            "succeeded={}, "
            "phase={}, "
            "sct={}, "
            "sc={}, "
            "crd={}, "
            "more={}, "
            "dnr={}",
            c.status,
            c.succeeded(),
            static_cast<unsigned int>(c.phase()),
            utils::to_underlying(c.status_code_type()),
            static_cast<unsigned int>(c.status_code()),
            static_cast<unsigned int>(c.command_retry_delay()),
            c.more(),
            c.do_not_retry()
        );
    }
};
}  // namespace std
