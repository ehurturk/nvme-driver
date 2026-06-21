#include <cstring>
#include <linux/vfio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>

#include <print>
#include <string_view>
#include <format>
#include <chrono>
#include <thread>

#include "include/nvme_spec.hpp"

namespace nvme
{
constexpr std::string_view NVME_PCI_ADDR{"0000:01:00.0"};
constexpr int NVME_GROUP_ID = 6;
} // namespace nvme

using namespace std::chrono_literals;

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

    assert(device_info.num_regions >= 1 && "No BAR Region is implemented.");

    // Map the BAR0 region
    vfio_region_info bar0_region{.argsz = sizeof(bar0_region)};
    bar0_region.index = VFIO_PCI_BAR0_REGION_INDEX;

    if (::ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &bar0_region) < 0) {
        std::println("Could not get BAR0 region info: {}", strerror(errno));
        close(container_fd);
        close(group_fd);
        close(device_fd);
        return EXIT_FAILURE;
    }

    void *bar0 = ::mmap(nullptr, bar0_region.size, PROT_READ | PROT_WRITE,
            MAP_SHARED, device_fd, bar0_region.offset);

    if (bar0 == MAP_FAILED) {
        std::println(
                "Could not mmap bar0 region to device: {}", strerror(errno));
        close(container_fd);
        close(group_fd);
        close(device_fd);
        return EXIT_FAILURE;
    }

    auto *controller_properties =
            reinterpret_cast<volatile nvme::NvmeBar0 *>(bar0);

    // Execute a volatile read into the memory mapped region to get
    // the CAP value (8 bytes in size).
    std::uint64_t cap = utils::MMIORead(controller_properties->cap);

    auto vs = utils::MMIORead(controller_properties->vs);
    auto minor = nvme::vs::MinorVersion::extract(vs);
    auto major = nvme::vs::MajorVersion::extract(vs);

    std::println("[INFO] NVMe Version: {}.{}", major, minor);

    // Configure the Controller

    volatile std::uint32_t cc = utils::MMIORead(controller_properties->cc);
    volatile std::uint32_t csts = utils::MMIORead(controller_properties->csts);

    auto capTimeout = nvme::cap::TIMEOUT::extract(cap);
    auto timeout = capTimeout * 500ms; // in milliseconds

    auto wait_for = [](std::predicate auto&& predicate, std::chrono::milliseconds timeout) -> bool {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }

            std::this_thread::sleep_for(1ms);
        }

        return true;
    };

    // First disable the controller, as AMS, MPS, and CSS has to be
    // written while the controller is disabled (EN = 0)
    cc = nvme::cc::EN::insert(cc, 0b0);
    utils::MMIOWrite(controller_properties->cc, cc);

    auto rdy_is = [&](unsigned want) -> bool {
        return nvme::csts::RDY::extract(utils::MMIORead(controller_properties->csts)) == want;
    };

    // Wait for ready bit to be cleared to 0 after EN is set to 0
    if (!wait_for([&] { return rdy_is(0); }, timeout)) {
        std::println("[TIMEOUT]: Controller did not become disabled.");
        close(device_fd);
        close(group_fd);
        close(container_fd);
        return EXIT_FAILURE;
    }

    auto capAms = nvme::cap::AMS::extract(cap);
    auto capMPSMax = nvme::cap::MPSMax::extract(cap);
    auto capMPSMin = nvme::cap::MPSMin::extract(cap);
    std::uint8_t capCss = nvme::cap::CSS::extract(cap);

    // Set CC.AMS
    uint8_t ccAms{};
    if (capAms == 0) {
        std::println("AMS: Vendor specific.");
        ccAms = 0b0; // TODO: 0b111 or 0b0?
    } else if (capAms == 1) {
        std::println("AMS: WRRUPC");
        ccAms = 0b1;
    } else {
        std::println("Unsupported AMS. Defaulting to round robin.");
        ccAms = 0b0;
    }

    // Set CC.MPS
    uint8_t ccMps{0};
    std::println("MPSMax: {}\nMPSMin: {}", capMPSMax, capMPSMin);

    if (ccMps < capMPSMin || ccMps > capMPSMax) {
        ccMps = capMPSMin;
        std::println("[INFO]: ccMps {} is out of bounds, setting it as "
                     "capMPSMin: {}",
                ccMps, capMPSMin);
    }

    // Set CC.CSS
    uint8_t ccCss{0};

    if (capCss & nvme::cap::css::NCSS_BIT) {
        std::println("NCSS Bit is present...");
        ccCss = nvme::cc::css::NCSS;
    } else {
        std::println("NVME Command Set is not found.");
        close(container_fd);
        close(group_fd);
        close(device_fd);
        return EXIT_FAILURE;
    }

    cc = nvme::cc::AMS::insert(cc, ccAms);
    cc = nvme::cc::MPS::insert(cc, ccMps);
    cc = nvme::cc::CSS::insert(cc, ccCss);

    // Enable controller
    cc = nvme::cc::EN::insert(cc, 0b1);

    utils::MMIOWrite(controller_properties->cc, cc);

    // Wait for the RDY bit to become 1 after EN is set to 1.
    if (!wait_for([&] { return rdy_is(1); }, timeout)) {
        std::println("[TIMEOUT]: Controller did not become enabled.");
        close(device_fd);
        close(group_fd);
        close(container_fd);
        return EXIT_FAILURE;
    }

    std::println("[INFO] Controller is enabled.");


    // Create an I/O Queue
    // 1) Initialize CC.IOCQES / CC.IOSQES

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
