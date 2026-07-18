#include <linux/vfio.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <print>
#include <string>

#include "dma/heap.hpp"
#include "nvme/controller.hpp"
#include "os/mmio.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"
#include "vfio/group.hpp"

namespace
{
const std::string NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int     NVME_GROUP_ID = 6;
} // namespace

int main()
{
    try {
        vfio::VfioContainer container;
        vfio::VfioGroup     group{std::format("/dev/vfio/{}", NVME_GROUP_ID), container};
        vfio::VfioDevice    device = group.get_device(NVME_PCI_ADDR);

        dma::DmaHeap dma_heap{container};

        os::MappedMemory bar0 = device.map_region(VFIO_PCI_BAR0_REGION_INDEX);

        nvme::Controller controller{{os::MmioRegion{bar0.data()}, dma_heap}};

        controller.configure();

        // TODO: identify controller, set features, create I/O queues
        //       (admin command execution path).
    } catch (const std::exception& e) {
        std::println("[FATAL]: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
