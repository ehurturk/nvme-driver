#pragma once

#include "os/mmio.hpp"

#include <cassert>
#include <cstdint>

namespace nvme {

// NVMe BAR0 registers
class ControllerRegisters {
public:
    struct Doorbells {
        volatile std::uint32_t* sq_tail;
        volatile std::uint32_t* cq_head;
    };

    explicit ControllerRegisters(os::MmioRegion bar0);

    std::uint64_t cap() const;

    std::uint64_t asq() const;

    void asq(std::uint64_t iova);

    std::uint64_t acq() const;

    void acq(std::uint64_t iova);

    std::uint32_t vs() const;

    std::uint32_t intms() const;

    void intms(std::uint32_t value);

    std::uint32_t intmc() const;

    void intmc(std::uint32_t value);

    std::uint32_t cc() const;

    void cc(std::uint32_t value);

    std::uint32_t csts() const;

    std::uint32_t nssr() const;

    void nssr(std::uint32_t value);

    std::uint32_t aqa() const;

    void aqa(std::uint32_t value);

    std::uint32_t cmbloc() const;

    std::uint32_t cmbsz() const;

    std::uint8_t dstrd() const;

    // SQyTDBL = DOORBELL_BASE + (2y)     * (4 << DSTRD)
    // CQyHDBL = DOORBELL_BASE + (2y + 1) * (4 << DSTRD)
    Doorbells doorbells(std::uint16_t queue_id) const;

private:
    os::MmioRegion bar0_;
    std::uint8_t dstrd_;
};

}  // namespace nvme
