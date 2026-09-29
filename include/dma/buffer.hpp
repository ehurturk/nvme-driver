#pragma once

#include "os/mmap.hpp"
#include "vfio/container.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace dma {

using IOVA = std::uint64_t;

// DmaBuffer: A buffer that is DMA'able across VFIO using IOMMU.
// Owns a memory buffer that is memory mapped using MAP_SHARED and MAP_ANONYMOUS flags.
// Owned memory buffer will have a mapping of memory process virtual address to the allocated
// IO virtual address via the provided VfioContainer
class DmaBuffer {
public:
    // create and map a anon+shared host memory, and then map the UVA to an IOVA via VfioContainer
    DmaBuffer(vfio::VfioContainer& vfio, std::size_t size, std::size_t alignment = 1);

    ~DmaBuffer();

    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;

    DmaBuffer(DmaBuffer&& other) noexcept;

    DmaBuffer& operator=(DmaBuffer&& other) noexcept;

    IOVA iova() const;

    std::byte* uva() const;

    std::size_t size() const;

private:
    vfio::VfioContainer* vfio_;
    os::MappedMemory memory_;
    IOVA iova_ = 0;
};

// non owning view to a DmaBuffer
struct DmaView {
    DmaView() = default;

    explicit DmaView(const DmaBuffer& buffer);

    DmaView(std::byte* u, IOVA i, std::size_t s);

    DmaView subview(std::size_t offset, std::size_t len) const;

    std::byte* uva = nullptr;
    IOVA iova = 0;
    std::size_t size = 0;
};

}  // namespace dma
