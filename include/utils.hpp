#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
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

    static constexpr T value_mask =
        Width == bits ? static_cast<T>(~T{0}) : static_cast<T>((T{1} << Width) - T{1});

    static constexpr T mask = static_cast<T>(value_mask << Offset);

    static constexpr T extract(T v) noexcept {
        return static_cast<T>((v >> Offset) & value_mask);
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

template<typename T>
constexpr T align_up(T value, T alignment) {
    static_assert(std::is_unsigned_v<T>);
    if (!std::has_single_bit(alignment)) {
        throw std::invalid_argument("Alignment must be a nonzero power of two");
    }
    if (value > std::numeric_limits<T>::max() - (alignment - 1)) {
        throw std::overflow_error("Aligned size exceeds address space");
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T>
constexpr T extract_bits(T value, uint8_t low_bit, uint8_t high_bit) {
    uint8_t length = high_bit - low_bit + 1;
    T mask = (length >= (sizeof(T) * 8)) ? static_cast<T>(~0ULL) : static_cast<T>((1ULL << length) - 1);
    
    return (value >> low_bit) & mask;
}

inline void hexdump(std::span<const std::byte> buffer) {
    size_t offset = 0;

    for (auto row: buffer | std::views::chunk(16)) {
        std::print("{:08X} ", offset);

        for (std::byte b: row) {
            std::print("{:02X} ", std::to_integer<std::uint8_t>(b));
        }

        for (size_t i = row.size(); i < 16; ++i) {
            std::print("   ");
        }
        std::print(" |");

        for (std::byte b : row) {
            auto c = std::to_integer<unsigned char>(b);
            if (c >= 32 && c <= 126) {
                std::print("{}", static_cast<char>(c));
            } else {
                std::print(".");
            }
        }
        std::print("|\n");

        offset += row.size();
    }
}

}  // namespace utils
