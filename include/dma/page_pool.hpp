#pragma once

#include "dma/buffer.hpp"
#include "vfio/container.hpp"
#include "vfio/dma_geometry.hpp"

#include <optional>
#include <vector>

namespace dma {

struct PageAllocation;

// a page pool that preallocates a list of pages at construction time
class PagePool {
public:
    PagePool(vfio::VfioContainer& container, std::size_t page_size, std::uint32_t page_count);
    ~PagePool();

    PagePool(const PagePool&) = delete;
    PagePool(PagePool&&) = delete;

    PagePool& operator=(const PagePool&) = delete;
    PagePool& operator=(PagePool&&) = delete;

    std::optional<PageAllocation> allocate();

private:
    void release(const PageAllocation& buffer);

    DmaBuffer region_;
    std::uint32_t page_count_;
    std::size_t page_size_;

    std::vector<std::uint32_t> free_list_;

    friend PageAllocation;
};

struct PageAllocation {
    ~PageAllocation();

    PageAllocation(PageAllocation&&) noexcept;
    PageAllocation& operator=(PageAllocation&&) noexcept;

    PageAllocation(const PageAllocation&) = delete;
    PageAllocation& operator=(const PageAllocation&) = delete;

private:
    PageAllocation(PagePool* pool, DmaView view, std::uint32_t idx) : data_(view), free_page_idx_{idx}, pool_{pool} {
    }

    DmaView data_;
    std::uint32_t free_page_idx_;
    PagePool* pool_;

    friend PagePool;
};

}  // namespace dma
