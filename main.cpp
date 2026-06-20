#include <linux/vfio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
        close(container_fd);
        return EXIT_FAILURE;
    }

    if (::ioctl(container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) <= 0) {
        std::println("No support for IOMMU driver needed.");
        close(container_fd);
        return EXIT_FAILURE;
    }

    // Open the group
    int group_fd = ::open(
            std::format("/dev/vfio/{}", nvme::NVME_GROUP_ID).c_str(), O_RDWR);

    // Test the group is viable and available
    vfio_group_status group_status{.argsz = sizeof(group_status)};
    if (::ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status)) {
        std::println("Group status can't be retrieved.");
        close(container_fd);
        close(group_fd);
        return EXIT_FAILURE;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        std::println(
                "Group {} is not viable (not all devices are bound fo VFIO)",
                nvme::NVME_GROUP_ID);
        close(container_fd);
        close(group_fd);

        return EXIT_FAILURE;
    }

    // Add the group to the container
    if (::ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd)) {
        std::println("Group can't be added to the container.");
        close(container_fd);
        close(group_fd);

        return EXIT_FAILURE;
    }

    // Enable IOMMU model we want (TYPE_1_IOMMU)
    if (::ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU)) {
        std::println("Container IOMMU type can't be set to TYPE1.");
        close(container_fd);
        close(group_fd);

        return EXIT_FAILURE;
    }

    vfio_iommu_type1_info iommu_info{.argsz = sizeof(iommu_info)};
    // Get IOMMU info, has these fields:
    //   - flags
    //   - iova_pgsizes
    //   - cap_offset
    if (::ioctl(container_fd, VFIO_IOMMU_GET_INFO, &iommu_info)) {
        std::println("Can't get info about IOMMU.");
        close(container_fd);
        close(group_fd);

        return EXIT_FAILURE;
    }

    // Allocate some space and setup a DMA mapping
    // TODO: is this needed now?
    //vfio_iommu_type1_dma_map dma_map{.argsz = sizeof(dma_map)};
    //constexpr size_t DMA_SIZE = 1024 * 1024;
    //void *dma_region = ::mmap(nullptr, DMA_SIZE, PROT_READ | PROT_WRITE,
    //                          MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

    //if (dma_region == MAP_FAILED) {
    //    std::println("DMA Mmap failed.");
    //    return EXIT_FAILURE;
    //}

    //dma_map.vaddr = static_cast<std::uint64_t>(
    //    reinterpret_cast<std::uintptr_t>(dma_region));
    //dma_map.size = DMA_SIZE;
    //dma_map.iova = 0;
    //dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

    //::ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dma_map);

    int device_fd = ::ioctl(
            group_fd, VFIO_GROUP_GET_DEVICE_FD, nvme::NVME_PCI_ADDR.data());

    // Test and setup the device
    vfio_device_info device_info{.argsz = sizeof(device_info)};

    if (::ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info)) {
        std::println("Can't get info about the device.");
        close(container_fd);
        close(group_fd);
        close(device_fd);
        return EXIT_FAILURE;
    }

    if (!(device_info.flags & VFIO_DEVICE_FLAGS_RESET) ||
            !(device_info.flags & VFIO_DEVICE_FLAGS_PCI)) {
        std::println(
                "[INFO]: Device doesn't support reset or is not a PCI device.");
        close(container_fd);
        close(group_fd);
        close(device_fd);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < device_info.num_regions; i++) {
        struct vfio_region_info reg{.argsz = sizeof(reg)};
        reg.index = i;
        if (::ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &reg)) {
            std::println("Can't get device region info.");
            close(container_fd);
            close(group_fd);
            close(device_fd);
            return EXIT_FAILURE;
        }

        if (reg.size == 0) {
            std::println("[INFO]: Region index {} is unimplemented.", i);
            continue;
        }

        if (reg.flags & VFIO_REGION_INFO_FLAG_MMAP) {
            std::println("[INFO]: Region supports MMAP.");
        }

        std::println("Region size: {}\nOffset: {}", reg.size, reg.offset);
    }

    //::ioctl(device_fd, VFIO_DEVICE_RESET);

    close(device_fd);
    close(group_fd);
    close(container_fd);

    return EXIT_SUCCESS;
}

// TODO:
//   - what are device regions?
//   - mmap size? iova? why do we mmap?
//   - iommu_type1 vs cdev
//   - irqs?
