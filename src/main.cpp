#include <assert.h>
#include <cstring>
#include <fcntl.h>
#include <linux/vfio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <format>
#include <print>
#include <string_view>
#include <version>

#include "../include/controller.hpp"
#include "../include/nvme_spec.hpp"

namespace nvme
{
constexpr std::string_view NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int              NVME_GROUP_ID = 6;
} // namespace nvme

using namespace std::chrono_literals;

int main()
{
    nvme::VfioContainer vfio_container{std::format("/dev/vfio/{}", nvme::NVME_GROUP_ID)};
    vfio_container.setup_device(nvme::NVME_PCI_ADDR);

    // Map the BAR0 region
    vfio_region_info bar0_region{.argsz = sizeof(bar0_region)};
    bar0_region.index = VFIO_PCI_BAR0_REGION_INDEX;

    if (::ioctl(vfio_container.device_fd(), VFIO_DEVICE_GET_REGION_INFO, &bar0_region) < 0) {
        std::println("Could not get BAR0 region info: {}", strerror(errno));
        return EXIT_FAILURE;
    }

    void* bar0 = ::mmap(nullptr, bar0_region.size, PROT_READ | PROT_WRITE, MAP_SHARED, vfio_container.device_fd(), bar0_region.offset);

    if (bar0 == MAP_FAILED) {
        std::println("Could not mmap bar0 region to device: {}", strerror(errno));
        return EXIT_FAILURE;
    }

    nvme::NVMeBar0 ctl_space{bar0};

    nvme::Controller controller{vfio_container, ctl_space};

    controller.configure();
    controller.enable();

    // Create an I/O Queue
    // 1) Initialize CC.IOCQES / CC.IOSQES

    //::ioctl(device_fd, VFIO_DEVICE_RESET);

    // Disable the controller
    controller.disable();

    //::ioctl(container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_map);

    return EXIT_SUCCESS;
}

// TODO:
// 1) Create admin queues (SQ, CQ) in host memory
// 2) Rethink about buffer allocaion & DMA'ing it.
//     - separate buffers for SQ, CQ?
// 3) IOVA calculations, and IOVA <-> UVA translations
//    for the ASQB and ACQB values.
// 4) Make lifetime management more obvious (make
//    the compat layer a bit nicer and tidier)
// 5) Organize shit. Don't overcomplicate things, but
//    be aware of the performance implications.
