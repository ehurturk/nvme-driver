#pragma once

#include "dma/buffer.hpp"
#include "utils.hpp"

#include <cstddef>

namespace dma {

class DmaHeap {
public:
    explicit DmaHeap(vfio::VfioContainer& container);

    // return a dma buffer with page-rounded size
    // NOTE: this is prone to fragmentation, in the future a DMA allocator may be used
    DmaBuffer alloc(std::size_t bytes, std::size_t alignment = 1);

    const vfio::DmaGeometry& geometry() const;

private:
    vfio::VfioContainer* container_;
};

}  // namespace dma
