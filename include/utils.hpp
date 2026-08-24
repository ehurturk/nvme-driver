#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

using DWRD = std::uint32_t;

namespace utils {

template<typename T, int Offset, int Width>
struct BitField {
    static_assert(std::is_unsigned_v<T>);
    static_assert(Offset >= 0);
    static_assert(Width > 0);

    static constexpr int bits = std::numeric_limits<T>::digits;
    static_assert(Offset + Width <= bits);

    using value_type = T;

    static constexpr T value_mask = Width == bits ? ~T{0} : ((T{1} << Width) - T{1});

    static constexpr T mask = value_mask << Offset;

    static constexpr T extract(T v) noexcept {
        return (v >> Offset) & value_mask;
    }

    template<typename U>
    static constexpr T insert(T reg, U value) noexcept {
        const auto v = static_cast<T>(value);
        return static_cast<T>((reg & ~mask) | ((v & value_mask) << Offset));
    }
};

template<typename T = std::uint32_t>
class DwordBuilder {
    T value_{0};

public:
    constexpr DwordBuilder() = default;

    constexpr explicit DwordBuilder(T init) : value_{init} {
    }

    template<typename bitfield>
    constexpr DwordBuilder& set(typename bitfield::value_type v) {
        value_ = bitfield::insert(value_, v);
        return *this;
    }

    constexpr T get() const noexcept {
        return value_;
    }

    constexpr operator T() const noexcept {
        return value_;
    }
};

template<typename Enum>
constexpr auto to_underlying(Enum e) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(e);
}

template<typename T>
constexpr T to_zero_based(T v) noexcept {
    static_assert(std::is_unsigned_v<T>);
    assert(v > 0);
    return static_cast<T>(v - 1);
}

template<typename T>
constexpr T to_ones_based(T v) noexcept {
    static_assert(std::is_unsigned_v<T>);
    assert(v < std::numeric_limits<T>::max());
    return static_cast<T>(v + 1);
}

template<typename T>
[[nodiscard]] T ReadVolatile(const volatile T& reg) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    return reg;
}

template<typename T>
void WriteVolatile(volatile T& reg, T value) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    reg = value;
}

constexpr std::size_t page_round_up(std::size_t bytes) {
    // TODO: retrieve the page size at runtime instead of hardcoding it
    static constexpr std::size_t PGSIZE = 4096;
    return (bytes + PGSIZE - 1) & ~(PGSIZE - 1);
}

}  // namespace utils
