#pragma once

#include "nvme/spec.hpp"
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

    explicit ControllerRegisters(os::MmioRegion bar0)
        : bar0_{bar0},
          dstrd_{static_cast<std::uint8_t>(cap::DSTRD::extract(bar0_.read64(bar0::CAP)))} {
    }

    std::uint64_t cap() const {
        return bar0_.read64(bar0::CAP);
    }

    std::uint64_t asq() const {
        return bar0_.read64(bar0::ASQ);
    }

    void asq(std::uint64_t iova) {
        assert((iova & 0xfff) == 0 && "ASQ base must be 4 KiB aligned");
        bar0_.write64(bar0::ASQ, iova);
    }

    std::uint64_t acq() const {
        return bar0_.read64(bar0::ACQ);
    }

    void acq(std::uint64_t iova) {
        assert((iova & 0xfff) == 0 && "ACQ base must be 4 KiB aligned");
        bar0_.write64(bar0::ACQ, iova);
    }

    std::uint32_t vs() const {
        return bar0_.read32(bar0::VS);
    }

    std::uint32_t intms() const {
        return bar0_.read32(bar0::INTMS);
    }

    void intms(std::uint32_t value) {
        bar0_.write32(bar0::INTMS, value);
    }

    std::uint32_t intmc() const {
        return bar0_.read32(bar0::INTMC);
    }

    void intmc(std::uint32_t value) {
        bar0_.write32(bar0::INTMC, value);
    }

    std::uint32_t cc() const {
        return bar0_.read32(bar0::CC);
    }

    void cc(std::uint32_t value) {
        bar0_.write32(bar0::CC, value);
    }

    std::uint32_t csts() const {
        return bar0_.read32(bar0::CSTS);
    }

    std::uint32_t nssr() const {
        return bar0_.read32(bar0::NSSR);
    }

    void nssr(std::uint32_t value) {
        bar0_.write32(bar0::NSSR, value);
    }

    std::uint32_t aqa() const {
        return bar0_.read32(bar0::AQA);
    }

    void aqa(std::uint32_t value) {
        bar0_.write32(bar0::AQA, value);
    }

    std::uint32_t cmbloc() const {
        return bar0_.read32(bar0::CMBLOC);
    }

    std::uint32_t cmbsz() const {
        return bar0_.read32(bar0::CMBSZ);
    }

    std::uint8_t dstrd() const {
        return dstrd_;
    }

    // SQyTDBL = DOORBELL_BASE + (2y)     * (4 << DSTRD)
    // CQyHDBL = DOORBELL_BASE + (2y + 1) * (4 << DSTRD)
    Doorbells doorbells(std::uint16_t queue_id) const {
        const std::size_t stride = std::size_t{4} << dstrd_;
        std::byte* base = bar0_.data() + bar0::DOORBELL_BASE;

        auto* sq_tail = reinterpret_cast<volatile std::uint32_t*>(base + (2u * queue_id) * stride);
        auto* cq_head =
            reinterpret_cast<volatile std::uint32_t*>(base + (2u * queue_id + 1u) * stride);

        return Doorbells{.sq_tail = sq_tail, .cq_head = cq_head};
    }

private:
    os::MmioRegion bar0_;
    std::uint8_t dstrd_;
};

}  // namespace nvme
