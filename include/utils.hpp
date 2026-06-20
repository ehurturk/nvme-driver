#pragma once

namespace utils {

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
void MMIOWrite(volatile T& reg, T value) noexcept {
  reg = value;
}

}  // namespace utils
