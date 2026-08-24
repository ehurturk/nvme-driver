#include "os/mmio.hpp"

#include <cstddef>
#include <cstdint>

namespace os {

MmioRegion::MmioRegion(void* base) noexcept : base_{static_cast<std::byte*>(base)} {
}

std::byte* MmioRegion::data() const noexcept {
    return base_;
}

std::uint32_t MmioRegion::read32(std::size_t offset) const noexcept {
    return reg<std::uint32_t>(offset);
}

void MmioRegion::write32(std::size_t offset, std::uint32_t value) const noexcept {
    reg<std::uint32_t>(offset) = value;
}

std::uint64_t MmioRegion::read64(std::size_t offset) const noexcept {
    const auto lo = read32(offset);
    const auto hi = read32(offset + 4);

    return std::uint64_t{lo} | (std::uint64_t{hi} << 32);
}

void MmioRegion::write64(std::size_t offset, std::uint64_t value) const noexcept {
    write32(offset, static_cast<std::uint32_t>(value));
    write32(offset + 4, static_cast<std::uint32_t>(value >> 32));
}

}  // namespace os
