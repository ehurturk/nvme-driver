#pragma once

#include "dma/buffer.hpp"
#include "nvme/command.hpp"
#include "nvme/io/types.hpp"
#include "utils.hpp"

namespace nvme::io::commands {

class Write {
public:
    /// @param read_buffer: specifies where the data is transferred from
    /// @param nsid: the namespace identifier to write to
    /// @param starting_lba: 64 bit address of the first logical block to be written
    /// @param nr_blocks_to_read: number of logical blocks to write, is 0s based
    /// @param management: indicates attributes for LBAs to be written, such as access latency,
    /// access frequency and other stuff
    /// @param limited_retry: if set to true then the controller applies limited retry efforst, if
    /// not the controller applies all recovery methods to write the data to NVM
    /// @param force_unit_access: if set to true then for any data the controller commits that data
    /// to non volatile media and returns the data that is written from the media with no implied
    /// ordering
    Write(
        dma::DmaView read_buffer,
        DWRD nsid,
        std::uint64_t starting_lba,
        std::uint16_t nr_blocks_to_write,
        DatasetManagement management = {},
        bool limited_retry = false,
        bool force_unit_access = false
    );

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept;

    struct Completion {
        static constexpr std::uint8_t CONFLICTING_ATTRIBUTES_STATUS_VAL = 0x80;
        static constexpr std::uint8_t INVALID_PROT_INFO_STATUS_VAL = 0x81;
        static constexpr std::uint8_t WRITE_TO_READ_ONLY_RANGE_STATUS_VAL = 0x82;

        bool conflicting_attributes = false;
        bool invalid_prot_information = false;
        bool write_to_ro_page = false;
    };

    static Completion decode_completion(const wire::CompletionEntry& c);

private:
    // TODO: CDW2, CDW3 bits 00-47 and CDW14 bits 00-31 represent LBST and ILBRT fields
    using CDW12_LIMITED_RETRY = utils::BitField<DWRD, 31, 1>;
    using CDW12_FORCE_UNIT_ACCESS = utils::BitField<DWRD, 30, 1>;
    using CDW12_PROTECTION_INFORMATION = utils::BitField<DWRD, 26, 3>;
    using CDW12_STORAGE_TAG_CHECK = utils::BitField<DWRD, 24, 1>;
    using CDW12_DIRECTIVE_TYPE = utils::BitField<DWRD, 20, 4>;
    using CDW12_NUMBER_OF_LOGICAL_BLOCKS = utils::BitField<DWRD, 0, 16>; // 0s based value

    using CDW13_DIRECTIVE_SPECIFIC = utils::BitField<DWRD, 16, 16>;
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

    using CDW15_LBATM = utils::BitField<DWRD, 16, 16>;
    using CDW15_LBAT = utils::BitField<DWRD, 0, 16>;

    // state:
    dma::DmaView _write_buffer; // DPTR
    DWRD _nsid;
    std::uint64_t _starting_lba;        // CDW10: bits 31:00, CDW11: bits 63:32
    std::uint16_t _nr_blocks_to_write;  // CDW12 bits 15:00
    std::uint32_t _dataset_management;  // CDW13 bits 07:00
    bool _limited_retry;                // CDW12 bit 31
    bool _force_unit_access;            // CDW12 bit 30
};

static_assert(CommandDescriptor<Write>);

}  // namespace nvme::io::commands
