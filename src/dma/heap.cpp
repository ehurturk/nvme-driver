#include "dma/heap.hpp"

#include "dma/buffer.hpp"
#include "utils.hpp"

#include <cstddef>

namespace dma {

DmaHeap::DmaHeap(vfio::VfioContainer& container) : container_{&container} {
}

DmaBuffer DmaHeap::alloc(std::size_t bytes) {
    return DmaBuffer{*container_, utils::page_round_up(bytes)};
}

}  // namespace dma
