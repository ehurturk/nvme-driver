#include "vfio/container.hpp"

#include "os/fd.hpp"
#include "os/mmap.hpp"
#include "utils.hpp"
#include "vfio/error.hpp"

#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <stdexcept>
#include <vector>

namespace vfio {

VfioContainer::VfioContainer() {
    fd_ = os::UniqueFd{::open("/dev/vfio/vfio", O_RDWR)};
    check(!fd_.valid(), "open /dev/vfio/vfio");

    check(
        ::ioctl(fd_.get(), VFIO_GET_API_VERSION) != VFIO_API_VERSION,
        "unexpected VFIO API version"
    );
    check(
        ::ioctl(fd_.get(), VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) == 0,
        "Type1 IOMMU not supported"
    );
}

int VfioContainer::fd() const {
    return fd_.get();
}

void VfioContainer::enable_type1_iommu() {
    if (iommu_enabled_) {
        return;
    }

    check(::ioctl(fd_.get(), VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) != 0, "VFIO_SET_IOMMU Type1");

    cache_iommu_info();
    seed_iova_cursor();
    iommu_enabled_ = true;
}

const DmaGeometry& VfioContainer::dma_geometry() const {
    if (!iommu_enabled_) {
        throw std::logic_error("DMA geometry requested before enabling the IOMMU");
    }
    return dma_geometry_;
}

std::uint64_t VfioContainer::map_dma(void* uva, std::size_t size, std::size_t alignment) {
    const auto& geometry = dma_geometry();
    alignment = geometry.allocation_alignment(alignment);
    
    if (uva == nullptr || size == 0 || reinterpret_cast<std::uintptr_t>(uva) % alignment != 0 ||
        size % geometry.mapping_alignment() != 0) {
        throw std::invalid_argument("DMA buffer does not satisfy mapping alignment");
    }

    const std::uint64_t iova = alloc_iova(size, alignment);

    vfio_iommu_type1_dma_map dma_map{};
    dma_map.argsz = sizeof(dma_map);
    dma_map.vaddr = reinterpret_cast<std::uint64_t>(uva);
    dma_map.size = size;
    dma_map.iova = iova;
    dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

    check(::ioctl(fd_.get(), VFIO_IOMMU_MAP_DMA, &dma_map) != 0, "VFIO_IOMMU_MAP_DMA");
    iova_cursor_ = iova + size;

    return iova;
}

void VfioContainer::unmap_dma(std::uint64_t iova, std::size_t size) {
    vfio_iommu_type1_dma_unmap u{};
    u.argsz = sizeof(u);
    u.iova = iova;
    u.size = size;

    if (::ioctl(fd_.get(), VFIO_IOMMU_UNMAP_DMA, &u) != 0) {
        std::println("[WARN] VFIO_IOMMU_UNMAP_DMA failed for iova {:#x}", iova);
    }
}

void VfioContainer::cache_iommu_info() {
    vfio_iommu_type1_info iommu_info{};
    iommu_info.argsz = sizeof(iommu_info);

    check(::ioctl(fd_.get(), VFIO_IOMMU_GET_INFO, &iommu_info) != 0, "VFIO_IOMMU_GET_INFO");

    if ((iommu_info.flags & VFIO_IOMMU_INFO_PGSIZES) == 0u) {
        throw std::runtime_error("VFIO did not report IOMMU page sizes");
    }
    dma_geometry_ = DmaGeometry::from_page_sizes(os::host_page_size(), iommu_info.iova_pgsizes);

    if (!(iommu_info.flags & VFIO_IOMMU_INFO_CAPS) || iommu_info.argsz <= sizeof(iommu_info)) {
        return;
    }

    // iommu_info.argsz sized contiguous buffer to hold the chain of headers
    std::vector<std::byte> buffer(iommu_info.argsz);

    auto* info = reinterpret_cast<vfio_iommu_type1_info*>(buffer.data());
    info->argsz = iommu_info.argsz;

    check(::ioctl(fd_.get(), VFIO_IOMMU_GET_INFO, info) != 0, "VFIO_IOMMU_GET_INFO (caps)");

    std::size_t offset = info->cap_offset;
    while (offset != 0) {
        auto* header = reinterpret_cast<vfio_info_cap_header*>(buffer.data() + offset);

        if (header->id == VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE) {
            auto* cap = reinterpret_cast<vfio_iommu_type1_info_cap_iova_range*>(header);
            for (std::uint32_t i = 0; i < cap->nr_iovas; ++i) {
                iova_ranges_.push_back({cap->iova_ranges[i].start, cap->iova_ranges[i].end});
            }
            break;
        }

        offset = header->next;
    }
}

bool VfioContainer::iova_range_valid(std::uint64_t base, std::size_t size) const {
    if (size == 0 || size > std::numeric_limits<std::uint64_t>::max() - base) {
        return false;
    }
    if (iova_ranges_.empty()) {
        return true;
    }

    const std::uint64_t last = base + size - 1;
    for (const auto& range : iova_ranges_) {
        if (base >= range.first && last <= range.last) {
            return true;
        }
    }
    return false;
}

void VfioContainer::seed_iova_cursor() {
    std::uint64_t base = iova_ranges_.empty() ? std::uint64_t{0x10000} : iova_ranges_.front().first;
    const auto alignment = static_cast<std::uint64_t>(dma_geometry_.mapping_alignment());
    base = std::max(base, alignment);
    iova_cursor_ = utils::align_up(base, alignment);
}

std::uint64_t VfioContainer::alloc_iova(std::size_t size, std::size_t alignment) const {
    std::uint64_t iova = utils::align_up(iova_cursor_, static_cast<std::uint64_t>(alignment));

    if (!iova_range_valid(iova, size)) {
        // snap to next range that fits as it doesnt fit in this iova range
        bool placed = false;
        for (const auto& range : iova_ranges_) {
            const auto base = std::max(iova, range.first);
            if (base > std::numeric_limits<std::uint64_t>::max() - (alignment - 1)) {
                continue;
            }
            const auto start = utils::align_up(base, static_cast<std::uint64_t>(alignment));
            if (start <= range.last && size != 0 &&
                size <= std::numeric_limits<std::uint64_t>::max() - start &&
                size - 1 <= range.last - start) {
                iova = start;
                placed = true;
                break;
            }
        }
        if (!placed) {
            throw std::runtime_error("out of IOVA space for DMA mapping");
        }
    }

    return iova;
}

}  // namespace vfio
