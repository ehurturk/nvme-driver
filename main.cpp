#include <linux/vfio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <print>
#include <string_view>
#include <format>

namespace nvme
{
constexpr std::string_view NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int NVME_GROUP_ID = 6;
} // namespace nvme

int main()
{
    // Create a new container
    int container_fd = ::open("/dev/vfio/vfio", O_RDWR);

    if (::ioctl(container_fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        std::println("Unknown VFIO API Version!");
        return EXIT_FAILURE;
    }

    if (!::ioctl(container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        std::println("No support for IOMMU driver needed.");
        return EXIT_FAILURE;
    }

    // Open the group
    int group_fd = ::open(
        std::format("/dev/vfio/{}", nvme::NVME_GROUP_ID).c_str(), O_RDWR);

    // Test the group is viable and available
    vfio_group_status group_status{.argsz = sizeof(group_status)};
    ::ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status);

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        std::println("Group {} is not viable (not all devices are bound fo VFIO)", nvme::NVME_GROUP_ID);
        return EXIT_FAILURE;
    }

    // Add the group to the container
    ::ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd);

    // Enable IOMMU model we want (TYPE_1_IOMMU)
    ::ioctl(container_fd, VFIO_SET_IOMMU,
            VFIO_TYPE1_IOMMU); // TODO: maybe cdev in future?

    vfio_iommu_type1_info iommu_info{.argsz = sizeof(iommu_info)};
    // Get IOMMU info, has these fields:
    //   - flags
    //   - iova_pgsizes
    //   - cap_offset
    ::ioctl(container_fd, VFIO_IOMMU_GET_INFO, &iommu_info);

    // Allocate some space and setup a DMA mapping
    // TODO: is this needed now?
    vfio_iommu_type1_dma_map dma_map{.argsz = sizeof(dma_map)};
    constexpr size_t DMA_SIZE = 1024 * 1024;
    void *dma_region = ::mmap(nullptr, DMA_SIZE, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

    if (dma_region == MAP_FAILED) {
        std::println("DMA Mmap failed.");
        return EXIT_FAILURE;
    }

    dma_map.vaddr = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(dma_region));
    dma_map.size = DMA_SIZE;
    dma_map.iova = 0;
    dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

    ::ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dma_map);

    int device_fd = ::ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, nvme::NVME_PCI_ADDR);

    // Test and setup the device
    vfio_device_info device_info{.argsz = sizeof(device_info)};
    ::ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info);

    if (device_info.flags & VFIO_DEVICE_FLAGS_RESET) {
        std::println("[INFO]: Device supports reset");
    }

    if (device_info.flags & VFIO_DEVICE_FLAGS_PCI) {
        std::println("[INFO]: Device is a VFIO-PCI device");
    }

    for (int i = 0; i < device_info.num_regions; i++) {
        struct vfio_region_info reg{.argsz = sizeof(reg)};
        reg.index = i;
        ::ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &reg);

    }

    ::ioctl(device_fd, VFIO_DEVICE_RESET);
}


// TODO:
//   - what are device regions?
//   - mmap size? iova? why do we mmap?
//   - iommu_type1 vs cdev
//   - irqs?
