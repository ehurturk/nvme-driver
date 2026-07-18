#pragma once

#include <sys/mman.h>
#include <sys/types.h>

#include <cstddef>
#include <system_error>
#include <utility>

namespace os
{

// RAII of mmaped region
class MappedMemory
{
  public:
    MappedMemory() = default;

    MappedMemory(std::size_t size, int prot, int flags, int fd, off_t offset) : size_{size}
    {
        void* addr = ::mmap(nullptr, size, prot, flags, fd, offset);
        if (addr == MAP_FAILED)
            throw std::system_error(errno, std::generic_category(), "mmap");
        data_ = static_cast<std::byte*>(addr);
    }

    // get MAP_ANONYMOUS | MAP_SHARED memory map with RW prots
    // its important that no copy on write pages are used, that's why I chose to use MAP_SHARED
    // also there is no file backing really, so MAP_ANONYMOUS
    static MappedMemory anonymous(std::size_t size)
    {
        return MappedMemory{size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0};
    }

    ~MappedMemory()
    {
        if (data_ != nullptr)
            ::munmap(data_, size_);
    }

    MappedMemory(const MappedMemory&)            = delete;
    MappedMemory& operator=(const MappedMemory&) = delete;

    MappedMemory(MappedMemory&& o) noexcept
            : data_{std::exchange(o.data_, nullptr)}, size_{std::exchange(o.size_, 0)}
    {}

    MappedMemory& operator=(MappedMemory&& o) noexcept
    {
        if (this != &o) {
            if (data_ != nullptr)
                ::munmap(data_, size_);
            data_ = std::exchange(o.data_, nullptr);
            size_ = std::exchange(o.size_, 0);
        }
        return *this;
    }

    std::byte* data() const
    {
        return data_;
    }

    std::size_t size() const
    {
        return size_;
    }

  private:
    std::byte*  data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace os
