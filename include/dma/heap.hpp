#pragma once

#include <cstddef>

#include "dma/buffer.hpp"
#include "utils.hpp"

namespace dma
{

class DmaHeap
{
  public:
    explicit DmaHeap(vfio::VfioContainer& container) : container_{&container}
    {}

    // return a dma buffer with page-rounded size
    // NOTE: this is prone to fragmentation, in the future a DMA allocator may be used
    DmaBuffer alloc(std::size_t bytes)
    {
        return DmaBuffer{*container_, utils::page_round_up(bytes)};
    }

  private:
    vfio::VfioContainer* container_;
};

} // namespace dma
