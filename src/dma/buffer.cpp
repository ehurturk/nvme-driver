#include "dma/buffer.hpp"

#include "os/mmap.hpp"
#include "vfio/container.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace dma {

DmaBuffer::DmaBuffer(vfio::VfioContainer& vfio, std::size_t size, std::size_t alignment)
    : vfio_{&vfio},
      memory_{os::MappedMemory::anonymous(
          vfio.dma_geometry().mapping_size(size),
          vfio.dma_geometry().allocation_alignment(alignment)
      )},
      iova_{vfio.map_dma(memory_.data(), memory_.size(), alignment)} {
}

DmaBuffer::~DmaBuffer() {
    if (vfio_ != nullptr) {
        vfio_->unmap_dma(iova_, memory_.size());
    }
}

DmaBuffer::DmaBuffer(DmaBuffer&& other) noexcept
    : vfio_{std::exchange(other.vfio_, nullptr)},
      memory_{std::move(other.memory_)},
      iova_{std::exchange(other.iova_, 0)} {
}

DmaBuffer& DmaBuffer::operator=(DmaBuffer&& other) noexcept {
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

IOVA DmaBuffer::iova() const {
    return iova_;
}

std::byte* DmaBuffer::uva() const {
    return memory_.data();
}

std::size_t DmaBuffer::size() const {
    return memory_.size();
}

DmaView::DmaView(const DmaBuffer& buffer)
    : uva{buffer.uva()}, iova{buffer.iova()}, size{buffer.size()} {
}

DmaView::DmaView(std::byte* u, IOVA i, std::size_t s) : uva{u}, iova{i}, size{s} {
}

DmaView DmaView::subview(std::size_t offset, std::size_t len) const {
    assert(offset + len <= size);
    return DmaView{uva + offset, iova + offset, len};
}

}  // namespace dma
