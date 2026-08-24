#pragma once

#include "os/mmap.hpp"
#include "vfio/container.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace dma {

using IOVA = std::uint64_t;

// DmaBuffer: A buffer that is DMA'able across VFIO using IOMMU.
// Owns a memory buffer that is memory mapped using MAP_SHARED and MAP_ANONYMOUS flags.
// Owned memory buffer will have a mapping of memory process virtual address to the allocated
// IO virtual address, via the provided VfioContainer.
class DmaBuffer {
public:
    // create and map a anon+shared host memory, and then map the UVA to an IOVA via VfioContainer
    DmaBuffer(vfio::VfioContainer& vfio, std::size_t size)
        : vfio_{&vfio},
          memory_{os::MappedMemory::anonymous(size)},
          iova_{vfio.map_dma(memory_.data(), size)} {
    }

    ~DmaBuffer() {
        if (vfio_ != nullptr) {
            vfio_->unmap_dma(iova_, memory_.size());
        }
    }

    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;

    DmaBuffer(DmaBuffer&& other) noexcept
        : vfio_{std::exchange(other.vfio_, nullptr)},
          memory_{std::move(other.memory_)},
          iova_{std::exchange(other.iova_, 0)} {
    }

    DmaBuffer& operator=(DmaBuffer&& other) noexcept {
        if (this != &other) {
            if (vfio_ != nullptr) {
                vfio_->unmap_dma(iova_, memory_.size());
            }
            vfio_ = std::exchange(other.vfio_, nullptr);
            memory_ = std::move(other.memory_);
            iova_ = std::exchange(other.iova_, 0);
        }
        return *this;
    }

    IOVA iova() const {
        return iova_;
    }

    std::byte* uva() const {
        return memory_.data();
    }

    std::size_t size() const {
        return memory_.size();
    }

private:
    vfio::VfioContainer* vfio_;
    os::MappedMemory memory_;
    IOVA iova_ = 0;
};

// non owning view to a DmaBuffer
struct DmaView {
    DmaView() = default;

    explicit DmaView(const DmaBuffer& buffer)
        : uva{buffer.uva()}, iova{buffer.iova()}, size{buffer.size()} {
    }

    DmaView(std::byte* u, IOVA i, std::size_t s) : uva{u}, iova{i}, size{s} {
    }

    DmaView subview(std::size_t offset, std::size_t len) const {
        assert(offset + len <= size);
        return DmaView{uva + offset, iova + offset, len};
    }

    std::byte* uva = nullptr;
    IOVA iova = 0;
    std::size_t size = 0;
};

}  // namespace dma
