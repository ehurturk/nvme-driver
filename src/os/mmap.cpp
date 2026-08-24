#include "os/mmap.hpp"

#include <sys/mman.h>
#include <sys/types.h>

#include <cerrno>
#include <cstddef>
#include <system_error>
#include <utility>

namespace os {

MappedMemory::MappedMemory(std::size_t size, int prot, int flags, int fd, off_t offset)
    : size_{size} {
    void* addr = ::mmap(nullptr, size, prot, flags, fd, offset);
    if (addr == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "mmap");
    }
    data_ = static_cast<std::byte*>(addr);
}

MappedMemory MappedMemory::anonymous(std::size_t size) {
    return MappedMemory{size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0};
}

MappedMemory::~MappedMemory() {
    if (data_ != nullptr) {
        ::munmap(data_, size_);
    }
}

MappedMemory::MappedMemory(MappedMemory&& o) noexcept
    : data_{std::exchange(o.data_, nullptr)}, size_{std::exchange(o.size_, 0)} {
}

MappedMemory& MappedMemory::operator=(MappedMemory&& o) noexcept {
    if (this != &o) {
        if (data_ != nullptr) {
            ::munmap(data_, size_);
        }
        data_ = std::exchange(o.data_, nullptr);
        size_ = std::exchange(o.size_, 0);
    }
    return *this;
}

std::byte* MappedMemory::data() const {
    return data_;
}

std::size_t MappedMemory::size() const {
    return size_;
}

}  // namespace os
