#pragma once

#include "dma_memory.hpp"

namespace nvme
{
struct DmaMemoryView
{
    DmaMemoryView() = default;
    explicit DmaMemoryView(const DmaMemory& dm) : uva(dm.uva()), iova{dm.iova()}, size{dm.size()}
    {}

    DmaMemoryView(std::byte* u, IOVA i, std::size_t s) : uva{u}, iova{i}, size{s}
    {}

    // create a subview out of this dma view that represents a subview of the
    // dma region that this views to
    DmaMemoryView subview(std::size_t offset, std::size_t len) const
    {
        assert(offset + len <= size);
        return DmaMemoryView{uva + offset, iova + offset, len};
    }

    std::byte*  uva  = nullptr;
    IOVA        iova = 0;
    std::size_t size = 0;
};

} // namespace nvme
