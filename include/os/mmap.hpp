#pragma once

#include <sys/mman.h>
#include <sys/types.h>

#include <cstddef>
#include <system_error>
#include <utility>

namespace os {

// RAII of mmaped region
class MappedMemory {
public:
    MappedMemory() = default;

    MappedMemory(std::size_t size, int prot, int flags, int fd, off_t offset);

    // get MAP_ANONYMOUS | MAP_SHARED memory map with RW prots
    // its important that no copy on write pages are used, that's why I chose to use MAP_SHARED
    // also there is no file backing really, so MAP_ANONYMOUS
    static MappedMemory anonymous(std::size_t size);

    ~MappedMemory();

    MappedMemory(const MappedMemory&) = delete;
    MappedMemory& operator=(const MappedMemory&) = delete;

    MappedMemory(MappedMemory&& o) noexcept;

    MappedMemory& operator=(MappedMemory&& o) noexcept;

    std::byte* data() const;

    std::size_t size() const;

private:
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace os
