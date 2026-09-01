#pragma once

#include "nvme/command.hpp"
#include "utils.hpp"

namespace nvme::io::commands {

class Read {
public:
    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept;

    struct Completion {
        static constexpr std::uint8_t CONFLICTING_ATTRIBUTES_STATUS_VAL = 0x80;
        static constexpr std::uint8_t INVALID_PROT_INFO_STATUS_VAL = 0x81;

        bool conflicting_attributes = false;
        bool invalid_prot_information = false;
    };

    static Completion decode_completion(const wire::CompletionEntry& c);

private:
    // TODO: CDW2, CDW3 bits 00-47 and CDW14 bits 00-31 represent ELBST and EILBRT fields
    // TODO: CDW10 is Starting LBA address first 32 bits, CDW11 is last 32 bits
    using CDW12_LIMITED_RETRY = utils::BitField<DWRD, 31, 1>;
    using CDW12_FORCE_UNIT_ACCESS = utils::BitField<DWRD, 30, 1>;
    using CDW12_PROTECTION_INFORMATION = utils::BitField<DWRD, 26, 3>;
    using CDW12_STORAGE_TAG_CHECK = utils::BitField<DWRD, 24, 1>;
    using CDW12_NUMBER_OF_LOGICAL_BLOCKS = utils::BitField<DWRD, 0, 16>; // 0s based value

    using CDW13_DSM_INCOMPRESSABLE = utils::BitField<DWRD, 7, 1>;
    using CDW13_DSM_SEQ_REQ = utils::BitField<DWRD, 6, 1>;
    using CDW13_DSM_ACCESS_LATENCY = utils::BitField<DWRD, 4, 2>; // 00: none, 01: idle, 10: normal, 11: low

    // 0000: no info
    // 0001: typical number of reads and writes expected
    // 0010: infrequent nr of reads and writes expected
    // 0011: infrequent nr of writes, frequent nr of reads expected
    // 0100: frequent nr of writes, infrequent nr of reads expected
    // 0101: frequent nr of write and reads expected
    // 0110: one time read
    // 0111: speculative read (command is part of a prefetch op)
    // 1000: this LBA range is going to be overwritten in near future
    using CDW13_DSM_ACCESS_FREQ = utils::BitField<DWRD, 0, 4>;

    using CDW15_ELBATM = utils::BitField<DWRD, 16, 16>;
    using CDW15_ELBAT = utils::BitField<DWRD, 0, 16>;

    // state:
    // TODO:
};

static_assert(CommandDescriptor<Read>);

} // namespace nvme::io::commands