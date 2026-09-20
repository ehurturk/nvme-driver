#include "dma/heap.hpp"

#include "dma/buffer.hpp"
#include "utils.hpp"

#include <cstddef>

namespace dma {

DmaHeap::DmaHeap(vfio::VfioContainer& container) : container_{&container} {
}

DmaBuffer DmaHeap::alloc(std::size_t bytes, std::size_t alignment) {
    return DmaBuffer{*container_, bytes, alignment};
}

const vfio::DmaGeometry& DmaHeap::geometry() const {
    return container_->dma_geometry();
}

}  // namespace dma
