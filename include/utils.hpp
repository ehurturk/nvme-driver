#pragma once

#include <cstdint>
#include <type_traits>
#include <numeric>

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
template <typename T, int Offset, int Width> struct BitField {
    static_assert(std::is_unsigned_v<T>);
    static_assert(Offset >= 0);
    static_assert(Width > 0);

    static constexpr int bits = std::numeric_limits<T>::digits;
    static_assert(Offset + Width <= bits);

    using value_type = T;

    static constexpr T value_mask =
            Width == bits ? ~T{0} : ((T{1} << Width) - T{1});

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
    {
    }

    template <typename BitField>
    constexpr DwordBuilder &set(typename BitField::value_type v)
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

template <typename T>
[[nodiscard]] T ReadVolatile(const volatile T &reg) noexcept
{
    return reg;
}

template <typename T> void WriteVolatile(volatile T &reg, T value) noexcept
{
    reg = value;
}

class MmioRegion
{
  public:
    explicit MmioRegion(void *base) : base_{reinterpret_cast<std::byte *>(base)}
    {
    }

    std::uint32_t read32(std::size_t offset) const
    {
        return ReadVolatile(base_ + offset);
    }

    void write32(std::size_t offset, std::uint32_t value) const
        {
            WriteVolatile(base_ + offset, value);
        }

std::uint32_t read64(std::size_t offset) const
        {
            auto lo = read32(off);
            auto hi = read32(off + 4);
            return std::uint64_t{lo} | (std::uint64_t{hi} << 32);
    }

    void write32(std::size_t offset, std::uint32_t value) const
        {
            WriteVolatile(base_ + offset, value);
    }

  private:
    std::byte *base_;
};

} // namespace utils
