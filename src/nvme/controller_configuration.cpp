#include "nvme/controller_configuration.hpp"

#include "nvme/spec.hpp"

#include <format>
#include <stdexcept>

namespace nvme {

ControllerConfiguration
ControllerConfiguration::select(std::uint64_t capabilities, const vfio::DmaGeometry& geometry) {
    const auto minimum = cap::MPSMin::extract(capabilities);
    const auto maximum = cap::MPSMax::extract(capabilities);
    if (minimum > maximum) {
        throw std::runtime_error("Invalid CAP.MPSMIN / CAP.MPSMAX range");
    }
    if (minimum != 0) {
        throw std::runtime_error(
            std::format(
                "Controller does not support 4 KiB NVMe pages (CAP.MPSMIN={}, CAP.MPSMAX={})",
                minimum,
                maximum
            )
        );
    }

    return {.dma = geometry, .nvme_page_size = std::size_t{1} << 12, .cc_mps = 0};
}

}  // namespace nvme
