#pragma once

#include <cstddef>
#include <cstdint>

namespace os
{

class MmioRegion
{
  public:
    explicit MmioRegion(void* base) noexcept : base_{static_cast<std::byte*>(base)}
    {}

    std::byte* data() const noexcept
    {
        return base_;
    }

    std::uint32_t read32(std::size_t offset) const noexcept
    {
        return reg<std::uint32_t>(offset);
    }

    void write32(std::size_t offset, std::uint32_t value) const noexcept
    {
        reg<std::uint32_t>(offset) = value;
    }

    std::uint64_t read64(std::size_t offset) const noexcept
    {
        const auto lo = read32(offset);
        const auto hi = read32(offset + 4);

        return std::uint64_t{lo} | (std::uint64_t{hi} << 32);
    }

    void write64(std::size_t offset, std::uint64_t value) const noexcept
    {
        write32(offset, static_cast<std::uint32_t>(value));
        write32(offset + 4, static_cast<std::uint32_t>(value >> 32));
    }

  private:
    template <typename T> volatile T& reg(std::size_t offset) const noexcept
    {
        return *reinterpret_cast<volatile T*>(base_ + offset);
    }

  private:
    std::byte* base_;
};

} // namespace os
