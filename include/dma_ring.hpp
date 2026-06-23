#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <utility>

namespace nvme
{
using IOVA = std::uint64_t;

// Describes the memory buffer of a DMA'd memory.
// Contains both the device-visible address (IOVA)
// and the user virtual address (UVA). User virtual address
// is needed to (obviously) access it through the host,
// whereas IOVA is used primarly for DMA and passing
// queue memory location to the controller.
class DmaMemoryDescriptor
{
  public:
    // mmaps aligned region from RAM via MAP_PRIVATE and MAP_ANONYMOUS with READ/WRITE PROT rules.
    explicit DmaMemoryDescriptor(std::size_t size) : size_(size)
    {
    }

    // munmaps the mapped DMA region
    ~DmaMemoryDescriptor()
    {
        if (uva_ != nullptr)
            ::munmap(uva_, size_);
        // TODO: should this also call VFIO_UNMAP_DMA?
    }

    DmaMemoryDescriptor(DmaMemoryDescriptor &&other) noexcept
            : uva_{std::exchange(other.uva_, nullptr)},
              iova_{std::exchange(other.iova_, 0)},
              size_{std::exchange(other.size_, 0)}
    {
    }

    DmaMemoryDescriptor(const DmaMemoryDescriptor &) = delete;
    DmaMemoryDescriptor &operator=(const DmaMemoryDescriptor &) = delete;

    // IOVA should only be set after VFIO_IOMMU_MAP_DMA call
    inline void set_iova(IOVA v)
    {
        iova_ = v;
    }

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
    std::byte *uva_; // userspace virtual addr
    IOVA iova_; // device virtual addr
    std::size_t size_; // size of the DMA buffer
};

}; // namespace nvme
