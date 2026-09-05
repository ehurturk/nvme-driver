#include "nvme/io/write.hpp"

#include "dma/buffer.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

namespace nvme::io::commands {

Write::Write(
    dma::DmaView read_buffer,
    std::uint64_t starting_lba,
    std::uint16_t nr_blocks_to_write,
    DatasetManagement management,
    bool limited_retry,
    bool force_unit_access
)
    : _read_buffer{read_buffer},
      _starting_lba{starting_lba},
      _nr_blocks_to_write{nr_blocks_to_write},
      _limited_retry{limited_retry},
      _force_unit_access{force_unit_access} {
    
    auto dataset_mgmt = utils::DwordBuilder<DWRD>{};
    
    dataset_mgmt.set<CDW13_DSM_INCOMPRESSABLE>(static_cast<DWRD>(management.incompressable));
    dataset_mgmt.set<CDW13_DSM_SEQ_REQ>(static_cast<DWRD>(management.seq_request));
    dataset_mgmt.set<CDW13_DSM_ACCESS_LATENCY>(static_cast<DWRD>(management.latency));
    dataset_mgmt.set<CDW13_DSM_ACCESS_FREQ>(static_cast<DWRD>(management.frequency));

    _dataset_management = dataset_mgmt.get();
}

wire::SubmissionEntry Write::build(std::uint16_t this_cid) const noexcept {
    wire::SubmissionEntry entry = build_base(Opcode::Write, this_cid);
    
    // get low bits of starting lba
    auto slba_lo = utils::extract_bits(_starting_lba, 0, 31);
    // get high bits of starting lba
    auto slba_hi = utils::extract_bits(_starting_lba, 32, 63);
    
    entry.cdw10 = static_cast<std::uint32_t>(slba_lo);
    entry.cdw11 = static_cast<std::uint32_t>(slba_hi);
    entry.cdw12 = utils::DwordBuilder<DWRD>{}.set<CDW12_LIMITED_RETRY>(static_cast<DWRD>(_limited_retry))
                                             .set<CDW12_FORCE_UNIT_ACCESS>(static_cast<DWRD>(_force_unit_access))
                                             .set<CDW12_NUMBER_OF_LOGICAL_BLOCKS>(static_cast<DWRD>(_nr_blocks_to_write));
    entry.cdw13 = _dataset_management;

    return entry;
}

Write::Completion Write::decode_completion(const wire::CompletionEntry& c) {
    Write::Completion comp;
    std::uint8_t status_val = c.status.status_code();

    comp.conflicting_attributes = status_val == Write::Completion::CONFLICTING_ATTRIBUTES_STATUS_VAL;
    comp.invalid_prot_information = status_val == Write::Completion::INVALID_PROT_INFO_STATUS_VAL;
    comp.write_to_ro_page = status_val == Write::Completion::WRITE_TO_READ_ONLY_RANGE_STATUS_VAL;

    return comp;
}

}  // namespace nvme::io::commands
