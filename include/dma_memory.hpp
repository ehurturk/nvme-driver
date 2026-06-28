#pragma once

#include "vfio_container.hpp"
#include <cassert>
#include <sys/ioctl.h>
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <utility>
#include <print>

#include <linux/vfio.h>

namespace nvme
{
using IOVA = std::uint64_t;

// Describes the memory buffer of a DMA'd memory.
// Contains both the device-visible address (IOVA)
// and the user virtual address (UVA). User virtual address
// is needed to (obviously) access it through the host,
// whereas IOVA is used primarly for DMA and passing
// queue memory location to the controller.
class DmaMemory
{
  public:
    // mmaps aligned region from RAM via MAP_SHARED | MAP_ANONYMOUS with READ/WRITE PROT rules.
    explicit DmaMemory(VfioContainer &vfio, std::size_t size)
            : vfio_{&vfio}, size_(size)
    {
        void *dma_region = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, 0, 0);
        if (dma_region == MAP_FAILED) {
            std::println("[ERROR]: DMA Memory mapping failed");
            throw std::system_error(
                    errno, std::generic_category(), "DMA mmap failed");
        }

        uva_ = static_cast<std::byte *>(dma_region);
        // NOTE: Based on my scheme, the returned iova_ should *probably* be equal to uva_.
        //       This is what SPDK/DPDK do, but its best to check it and assign a proper IOVA
        //       allocator.
        iova_ = vfio_->map_dma(dma_region, size);
    }

    // munmaps the mapped DMA region
    ~DmaMemory()
    {
        if (uva_ != nullptr) {
            if (vfio_ != nullptr)
                vfio_->unmap_dma(iova_, size_);
            ::munmap(uva_, size_);
        }
    }

    DmaMemory(DmaMemory &&other) noexcept
            : vfio_{std::exchange(other.vfio_, nullptr)},
              uva_{std::exchange(other.uva_, nullptr)},
              iova_{std::exchange(other.iova_, 0)},
              size_{std::exchange(other.size_, 0)}
    {
    }

    DmaMemory(const DmaMemory &) = delete;
    DmaMemory &operator=(const DmaMemory &) = delete;

    inline IOVA iova() const
    {
        return iova_;
    }

    inline std::byte *uva() const
    {
        return uva_;
    }

    inline std::size_t size() const
    {
        return size_;
    }

  private:
    VfioContainer *vfio_; // vfio container that will do DMA
    std::byte *uva_; // userspace virtual addr
    IOVA iova_; // device virtual addr
    std::size_t size_; // size of the DMA buffer
};

}; // namespace nvme
