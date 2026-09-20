#include "os/mmap.hpp"

#include "utils.hpp"

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace os {

std::size_t host_page_size() {
    const long size = ::sysconf(_SC_PAGESIZE);
    if (size <= 0 || !std::has_single_bit(static_cast<unsigned long>(size))) {
        throw std::runtime_error("Could not determine host page size");
    }
    return static_cast<std::size_t>(size);
}

MappedMemory::MappedMemory(std::size_t size, int prot, int flags, int fd, off_t offset)
    : size_{size} {
    void* addr = ::mmap(nullptr, size, prot, flags, fd, offset);
    if (addr == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "mmap");
    }
    data_ = static_cast<std::byte*>(addr);
}

MappedMemory MappedMemory::anonymous(std::size_t size, std::size_t alignment) {
    if (size == 0 || !std::has_single_bit(alignment)) {
        throw std::invalid_argument("Invalid anonymous mapping size or alignment");
    }

    const auto page_size = host_page_size();
    alignment = std::max(alignment, page_size);
    size = utils::align_up(size, page_size);
    const auto padding = alignment - page_size;
    if (size > std::numeric_limits<std::size_t>::max() - padding) {
        throw std::overflow_error("Aligned mapping exceeds address space");
    }

    MappedMemory memory{size + padding, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0};
    const auto address = reinterpret_cast<std::uintptr_t>(memory.data_);
    const auto aligned = utils::align_up(address, static_cast<std::uintptr_t>(alignment));
    const auto prefix = static_cast<std::size_t>(aligned - address);
    const auto suffix = memory.size_ - prefix - size;

    if (prefix != 0) {
        if (::munmap(memory.data_, prefix) != 0) {
            throw std::system_error(errno, std::generic_category(), "munmap alignment prefix");
        }
        memory.data_ += prefix;
        memory.size_ -= prefix;
    }
    if (suffix != 0) {
        if (::munmap(memory.data_ + size, suffix) != 0) {
            throw std::system_error(errno, std::generic_category(), "munmap alignment suffix");
        }
        memory.size_ -= suffix;
    }
    return memory;
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
