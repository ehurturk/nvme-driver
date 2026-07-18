#pragma once

#include <bitset>
#include <concepts>
#include <cstdlib>
#include <cstring>
#include <version>

// compat layer for type punning
namespace compat
{
template <class T> [[nodiscard]] T* load_object_from_bytes(void* src) noexcept;

#if defined(__cpp_lib_start_lifetime_as) // C++23 & start_lifetime_as is defined
template <class T> [[nodiscard]] T* load_object_from_bytes(void* src) noexcept
{
    std::byte* raw_addr = reinterpret_cast<std::byte*>(src);
    return std::start_lifetime_as<T>(raw_addr);
}
#elif __cplusplus >= 202002L // C++20
template <class T>
    requires(std::is_trivially_copyable_v<T>)
[[nodiscard]] T* load_object_from_bytes(void* src) noexcept
{
    return std::launder(static_cast<T*>(std::memmove(src, src, sizeof(T))));
}
#else
#error "Need at least C++20"
#endif
} // namespace compat
