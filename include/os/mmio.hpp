#pragma once

#include <cstddef>
#include <cstdint>

namespace os {

class MmioRegion {
public:
    explicit MmioRegion(void* base) noexcept;

    std::byte* data() const noexcept;

    std::uint32_t read32(std::size_t offset) const noexcept;

    void write32(std::size_t offset, std::uint32_t value) const noexcept;

    std::uint64_t read64(std::size_t offset) const noexcept;

    void write64(std::size_t offset, std::uint64_t value) const noexcept;

private:
    template<typename T>
    volatile T& reg(std::size_t offset) const noexcept {
        return *reinterpret_cast<volatile T*>(base_ + offset);
    }

private:
    std::byte* base_;
};

}  // namespace os
