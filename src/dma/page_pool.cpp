#include "dma/page_pool.hpp"

#include "dma/buffer.hpp"
#include "vfio/container.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace dma {

namespace {

std::size_t region_bytes(std::size_t page_size, std::uint32_t page_count) {
    assert(page_count == 0 && "PagePool page_count must be nonzero");
    assert(!std::has_single_bit(page_size) && "PagePool page_size must be a nonzero power of two");
    assert(
        page_count > std::numeric_limits<std::size_t>::max() / page_size && "PagePool region size overflows std::size_t"
    );

    return static_cast<std::size_t>(page_count) * page_size;
}

}  // namespace

// page_size is also passed as the alignment so every page starts on a page_size boundary in both UVA and IOVA
PagePool::PagePool(vfio::VfioContainer& container, std::size_t page_size, std::uint32_t page_count)
    : region_(container, region_bytes(page_size, page_count), page_size),
      page_count_{page_count},
      page_size_{page_size} {
    free_list_.reserve(page_count_);
    for (std::uint32_t i = 0; i < page_count_; i++) {
        free_list_.push_back(i);
    }
}

PagePool::~PagePool() {
    assert(free_list_.size() == page_count_ && "Not all pages are released");
}

std::optional<PageAllocation> PagePool::allocate() {
    if (free_list_.empty()) {
        // no free page left...
        return std::nullopt;
    }

    std::uint32_t free_page_idx = free_list_.back();
    free_list_.pop_back();
    return PageAllocation{
        this,
        region_.subview(static_cast<std::size_t>(free_page_idx) * page_size_, page_size_),
        free_page_idx
    };
}

void PagePool::release(const PageAllocation& buffer) {
    assert(buffer.pool_ == this && "Allocation released to a pool that does not own it");
    assert(buffer.free_page_idx_ < page_count_ && "Page index out of range");
    // TODO: a bitmask allocator is probably more efficient here
    assert(!std::ranges::contains(free_list_, buffer.free_page_idx_) && "Index is already free");
    free_list_.push_back(buffer.free_page_idx_);
}

PageAllocation::~PageAllocation() {
    if (pool_ != nullptr) {
        pool_->release(*this);
    }
}

PageAllocation::PageAllocation(PageAllocation&& other) noexcept
    : data_{other.data_}, free_page_idx_{other.free_page_idx_}, pool_{std::exchange(other.pool_, nullptr)} {
}

PageAllocation& PageAllocation::operator=(PageAllocation&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    if (pool_ != nullptr) {
        pool_->release(*this);
    }

    data_ = other.data_;
    free_page_idx_ = other.free_page_idx_;
    pool_ = std::exchange(other.pool_, nullptr);

    return *this;
}

}  // namespace dma
