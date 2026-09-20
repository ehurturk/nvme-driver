#pragma once

#include "utils.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vfio {

struct DmaGeometry {
    std::size_t host_page_size;
    std::uint64_t iommu_page_sizes;
    std::size_t iommu_mapping_granularity;

    static DmaGeometry from_page_sizes(std::size_t host_page_size, std::uint64_t iommu_page_sizes) {
        if (!std::has_single_bit(host_page_size) || iommu_page_sizes == 0) {
            throw std::invalid_argument("Invalid host or IOMMU page sizes");
        }

        const auto granularity = std::uint64_t{1} << std::countr_zero(iommu_page_sizes);
        if (granularity > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument("IOMMU mapping granularity exceeds host address space");
        }

        return {.host_page_size=host_page_size, .iommu_page_sizes=iommu_page_sizes, .iommu_mapping_granularity=static_cast<std::size_t>(granularity)};
    }

    std::size_t mapping_alignment() const {
        return std::max(host_page_size, iommu_mapping_granularity);
    }

    std::size_t allocation_alignment(std::size_t requested) const {
        if (!std::has_single_bit(requested)) {
            throw std::invalid_argument("DMA alignment must be a nonzero power of two");
        }
        return std::max(mapping_alignment(), requested);
    }

    std::size_t mapping_size(std::size_t bytes) const {
        if (bytes == 0) {
            throw std::invalid_argument("DMA allocation must not be empty");
        }
        return utils::align_up(bytes, mapping_alignment());
    }
};

}  // namespace vfio
