#pragma once

#include <cstdint>
#include <numeric>
#include <type_traits>

using DWRD = std::uint32_t;

namespace utils
{

// BitField represents a bit field in a register. The offset
// and the width of this bit field in the register are defined
// by the Offset and the Width template parameters.
//
// The bit field value in this register can be extracted via a
// call to `extract`, providing the entire register as the value.
//
// The bit field value can be updated via a call to `insert`, which
// returns a new register value with that bit field replaced (for
// writes).
//
// The mask constant is defined via ::mask.
template <typename T, int Offset, int Width> struct BitField
{
    static_assert(std::is_unsigned_v<T>);
    static_assert(Offset >= 0);
    static_assert(Width > 0);

    static constexpr int bits = std::numeric_limits<T>::digits;
    static_assert(Offset + Width <= bits);

    using value_type = T;

    static constexpr T value_mask = Width == bits ? ~T{0} : ((T{1} << Width) - T{1});

    static constexpr T mask = value_mask << Offset;

    static constexpr T extract(T v) noexcept
    {
        return (v >> Offset) & value_mask;
    }

    template <typename U> static constexpr T insert(T reg, U value) noexcept
    {
        const auto v = static_cast<T>(value);
        return static_cast<T>((reg & ~mask) | ((v & value_mask) << Offset));
    }
};

template <typename T = std::uint32_t> class DwordBuilder
{
    T value_{0};

  public:
    constexpr DwordBuilder() = default;
    constexpr explicit DwordBuilder(T init) : value_{init}
    {}

    template <typename BitField> constexpr DwordBuilder& set(typename BitField::value_type v)
    {
        value_ = BitField::insert(value_, v);
        return *this;
    }

    constexpr T get() const noexcept
    {
        return value_;
    }
    constexpr operator T() const noexcept
    {
        return value_;
    }
};

template <typename Enum> constexpr auto to_underlying(Enum e) noexcept
{
    return static_cast<std::underlying_type_t<Enum>>(e);
}

template <typename T> [[nodiscard]] T ReadVolatile(const volatile T& reg) noexcept
{
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    return reg;
}

template <typename T> void WriteVolatile(volatile T& reg, T value) noexcept
{
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    reg = value;
}

class MmioRegion
{
  public:
    explicit MmioRegion(void* base) noexcept : base_{static_cast<std::byte*>(base)}
    {}

    operator void*() const { return base_; }

    std::uint32_t read32(std::size_t offset) const noexcept
    {
        return ReadVolatile(reg<std::uint32_t>(offset));
    }

    void write32(std::size_t offset, std::uint32_t value) const noexcept
    {
        WriteVolatile(reg<std::uint32_t>(offset), value);
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

// Round up given size (in bytes) to a mutiple of a page size
constexpr std::size_t page_round_up(std::size_t bytes)
{
    // TODO
    static constexpr std::size_t PGSIZE = 4096;
    return bytes & (PGSIZE);

}

} // namespace utils
