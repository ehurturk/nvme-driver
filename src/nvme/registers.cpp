#include "nvme/registers.hpp"

#include "nvme/spec.hpp"
#include "os/mmio.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace nvme {

ControllerRegisters::ControllerRegisters(os::MmioRegion bar0)
    : bar0_{bar0}, dstrd_{static_cast<std::uint8_t>(cap::DSTRD::extract(bar0_.read64(bar0::CAP)))} {
}

std::uint64_t ControllerRegisters::cap() const {
    return bar0_.read64(bar0::CAP);
}

std::uint64_t ControllerRegisters::asq() const {
    return bar0_.read64(bar0::ASQ);
}

void ControllerRegisters::asq(std::uint64_t iova) {
    assert((iova & 0xfff) == 0 && "ASQ base must be 4 KiB aligned");
    bar0_.write64(bar0::ASQ, iova);
}

std::uint64_t ControllerRegisters::acq() const {
    return bar0_.read64(bar0::ACQ);
}

void ControllerRegisters::acq(std::uint64_t iova) {
    assert((iova & 0xfff) == 0 && "ACQ base must be 4 KiB aligned");
    bar0_.write64(bar0::ACQ, iova);
}

std::uint32_t ControllerRegisters::vs() const {
    return bar0_.read32(bar0::VS);
}

std::uint32_t ControllerRegisters::intms() const {
    return bar0_.read32(bar0::INTMS);
}

void ControllerRegisters::intms(std::uint32_t value) {
    bar0_.write32(bar0::INTMS, value);
}

std::uint32_t ControllerRegisters::intmc() const {
    return bar0_.read32(bar0::INTMC);
}

void ControllerRegisters::intmc(std::uint32_t value) {
    bar0_.write32(bar0::INTMC, value);
}

std::uint32_t ControllerRegisters::cc() const {
    return bar0_.read32(bar0::CC);
}

void ControllerRegisters::cc(std::uint32_t value) {
    bar0_.write32(bar0::CC, value);
}

std::uint32_t ControllerRegisters::csts() const {
    return bar0_.read32(bar0::CSTS);
}

std::uint32_t ControllerRegisters::nssr() const {
    return bar0_.read32(bar0::NSSR);
}

void ControllerRegisters::nssr(std::uint32_t value) {
    bar0_.write32(bar0::NSSR, value);
}

std::uint32_t ControllerRegisters::aqa() const {
    return bar0_.read32(bar0::AQA);
}

void ControllerRegisters::aqa(std::uint32_t value) {
    bar0_.write32(bar0::AQA, value);
}

std::uint32_t ControllerRegisters::cmbloc() const {
    return bar0_.read32(bar0::CMBLOC);
}

std::uint32_t ControllerRegisters::cmbsz() const {
    return bar0_.read32(bar0::CMBSZ);
}

std::uint8_t ControllerRegisters::dstrd() const {
    return dstrd_;
}

ControllerRegisters::Doorbells ControllerRegisters::doorbells(std::uint16_t queue_id) const {
    const std::size_t stride = std::size_t{4} << dstrd_;
    std::byte* base = bar0_.data() + bar0::DOORBELL_BASE;

    auto* sq_tail = reinterpret_cast<volatile std::uint32_t*>(base + (2u * queue_id) * stride);
    auto* cq_head = reinterpret_cast<volatile std::uint32_t*>(base + (2u * queue_id + 1u) * stride);

    return Doorbells{.sq_tail = sq_tail, .cq_head = cq_head};
}

}  // namespace nvme
