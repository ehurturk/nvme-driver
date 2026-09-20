#pragma once

#include "vfio/dma_geometry.hpp"

#include <cstddef>
#include <cstdint>

namespace nvme {

struct ControllerConfiguration {
    vfio::DmaGeometry dma;
    std::size_t nvme_page_size;
    std::uint8_t cc_mps;

    static ControllerConfiguration select(std::uint64_t cap, const vfio::DmaGeometry& geometry);
};

}  // namespace nvme
