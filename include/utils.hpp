#pragma once

namespace utils {

template <typename T, int Offset, int Width>
struct BitField {
  static_assert(Offset >= 0 && Width > 0);
  static_assert(Offset + Width <= static_cast<int>(sizeof(T) * 8));

  static constexpr T mask = ((T{1} << Width) - 1) << Offset;

  static constexpr T extract(T v) noexcept {
    return (v >> Offset) & ((T{1} << Width) - 1);
  }

  static constexpr T insert(T reg, T value) noexcept {
    return (reg & ~mask) | ((value << Offset) & mask);
  }
};

template <typename T>
[[nodiscard]] T MMIORead(const volatile T& reg) noexcept {
  return reg;
}

template <typename T>
void MMIOWrite(const volatile T& reg, T value) noexcept {
  reg = value;
}

}  // namespace utils
