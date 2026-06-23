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
#include <version>
#include <concepts>

#include "include/admin_queue.hpp"
#include "include/nvme_spec.hpp"

// compat layer for type punning
namespace compat
{
template <class T> [[nodiscard]] T *load_object_from_bytes(void *src) noexcept;

#if defined(__cpp_lib_start_lifetime_as) // C++23 & start_lifetime_as is defined
template <class T> [[nodiscard]] T *load_object_from_bytes(void *src) noexcept
{
    std::byte *raw_addr = reinterpret_cast<std::byte *>(src);
    return std::start_lifetime_as<T>(raw_addr);
}
#elif __cplusplus >= 202002L // C++20
template <class T>
    requires(std::is_trivially_copyable_v<T>)
[[nodiscard]] T *load_object_from_bytes(void *src) noexcept
{
    return std::launder(static_cast<T *>(std::memmove(src, src, sizeof(T))));
}
#else
#error "Need at least C++20"
#endif
} // namespace compat

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

    // Retrieve valid IOVA address range

    // Allocate some space and setup a DMA mapping.
    // This mmap'd and IOMMU-mapped region is the pool of
    // host memory the controller can reach via DMA. This
    // memory mapped region will eventually include:
    //    - Queue memory (admin and I/O queues)
    //    - Data buffers
    //    - PRP Lists
    vfio_iommu_type1_dma_map dma_map{.argsz = sizeof(dma_map)};
    constexpr size_t DMA_SIZE = 1024 * 1024;
    void *dma_region = ::mmap(nullptr, DMA_SIZE, PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, 0, 0); // TODO: MAP_SHARED or MAP_PRIVATE?

    if (dma_region == MAP_FAILED) {
        std::println("DMA Mmap failed.");
        return EXIT_FAILURE;
    }

    std::uintptr_t iova_base = reinterpret_cast<std::uintptr_t>(dma_region);
    {
        std::uintptr_t start_iova = iova_base;
        std::uintptr_t end_iova = start_iova + DMA_SIZE - 1;

        // check alignment of the dma mapped RAM buffer
        if (start_iova % 4096 != 0 || DMA_SIZE % 4096 != 0) {
            close(group_fd);
            close(container_fd);
            std::println("[ERROR]: Validation error: UVA or size is not 4KB "
                         "page aligned.");
            return EXIT_FAILURE;
        }

        vfio_iommu_type1_info *info =
                static_cast<vfio_iommu_type1_info *>(malloc(iommu_info.argsz));
        info->argsz = iommu_info.argsz;

        if (::ioctl(container_fd, VFIO_IOMMU_GET_INFO, info) < 0) {
            close(container_fd);
            close(group_fd);
            free(info);
            std::println("Failed to get IOMMU info: {}", strerror(errno));
            return EXIT_FAILURE;
        }

        if (info->flags & VFIO_IOMMU_INFO_CAPS) {
            std::byte *raw_cap_addr =
                    reinterpret_cast<std::byte *>(info) + info->cap_offset;
            auto *header = compat::load_object_from_bytes<vfio_info_cap_header>(
                    raw_cap_addr);
            bool uva_fits_in_iova_ranges = false;

            while (header != nullptr) {
                if (header->id == VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE) {
                    // Then we can get the IOVA capability which allows us to
                    // report the valid IOVA ranges so that our DMA IOVAs are
                    // in the correct range.
                    //
                    // Note from linux/vfio.h:
                    //    "Any DMA map attempt outside the valid iova range
                    //     will return error."

                    // No UB since vfio_iommu_type1_info_cap_iova_range is a
                    // standard-layout, and header it share the same memory layout.
                    auto *cap = reinterpret_cast<
                            vfio_iommu_type1_info_cap_iova_range *>(header);

                    for (int i = 0; i < cap->nr_iovas; i++) {
                        std::uint64_t range_start = cap->iova_ranges[i].start;
                        std::uint64_t range_end = cap->iova_ranges[i].end;

                        if (start_iova >= range_start &&
                                end_iova <= range_end) {
                            // found the IOVA window, can set it
                            std::println("Found IOVA range: {} - {}. UVA is {}",
                                    range_start, range_end, iova_base);
                            uva_fits_in_iova_ranges = true;
                        }
                    }
                    break;
                }

                if (header->next) {
                    std::byte *next_addr =
                            reinterpret_cast<std::byte *>(info) + header->next;
                    header = compat::load_object_from_bytes<
                            vfio_info_cap_header>(next_addr);
                } else {
                    header = nullptr;
                }
            }

            if (!uva_fits_in_iova_ranges) {
                std::println(
                        "[INFO]: Could not find a valid IOVA range that fits "
                        "the UVA. Setting DMA IOVA base address as 0x0.");
                iova_base = 0x0;
            }
        }

        free(info);
    }

    // TODO: Replace this memory region with a memory allocator + DMA
    // mapper in the future.
    dma_map.vaddr = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(dma_region));
    dma_map.size = DMA_SIZE;
    dma_map.iova =
            iova_base; // TODO: change this in the future when allocator is implemented.
    dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

    if (::ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dma_map) < 0) {
        close(container_fd);
        close(group_fd);
        std::println("[ERROR]: IOMMU DMA Map failed: {}", strerror(errno));
        return EXIT_FAILURE;
    }

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
    assert(capTimeout <= 0xFF && "Maximum timeout value is 0xFF.");

    auto timeout = capTimeout * 500ms; // in milliseconds

    auto wait_for = [](std::predicate auto &&predicate,
                            std::chrono::milliseconds timeout) -> bool {
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
        return nvme::csts::RDY::extract(
                       utils::MMIORead(controller_properties->csts)) == want;
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

    // Insert AS, MPS, CSS into CC
    cc = nvme::cc::AMS::insert(cc, ccAms);
    cc = nvme::cc::MPS::insert(cc, ccMps);
    cc = nvme::cc::CSS::insert(cc, ccCss);

    // Set CC.IOSQES / CC.IOCQES
    // For creating the admin queues, I will set IOSQES/IOCQES to standard
    // NVM Command Set values (64 and 16 bytes respectively), and after running
    // Identify Controller command, I will adjust these to the correct required
    // minimums to create the I/O queues.
    cc = nvme::cc::IOSQES::insert(cc, nvme::constants::IO_SQES);
    cc = nvme::cc::IOCQES::insert(cc, nvme::constants::IO_CQES);

    // Setup AQA & ASQ & ACQ

    volatile uint32_t aqa = utils::MMIORead(controller_properties->aqa);
    aqa = nvme::aqa::ASQS::insert(aqa, nvme::constants::AQ_DEPTH);
    aqa = nvme::aqa::ACQS::insert(aqa, nvme::constants::AQ_DEPTH);

    // TODO: Populate adminQueue queues with IOVA and UVA.
    nvme::AdminQueuePair adminPair{};

    volatile uint64_t asq = utils::MMIORead(controller_properties->asq);
    std::uint64_t asqb = adminPair.submission_queues_.front().mem_.iova;
    assert(asqb % (1 << ccMps) == 0 && "ASQB shall be memory page aligned (based on the value in CC.MPS)");
    asq = nvme::asq::ASQB::insert(asq, asqb);

    volatile uint64_t acq = utils::MMIORead(controller_properties->acq);
    std::uint64_t acqb = adminPair.completion_queue_.mem_.iova;
    assert(acqb % (1 << ccMps) == 0 && "ACQB shall be memory page aligned (based on the value in CC.MPS)");
    acq = nvme::acq::ACQB::insert(acq, acqb);

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

    // Disable the controller
    cc = nvme::cc::EN::insert(cc, 0b0);

    utils::MMIOWrite(controller_properties->cc, cc);

    if (!wait_for([&] { return rdy_is(0); }, timeout)) {
        std::println("[TIMEOUT]: Controller did not become disabled.");
        close(device_fd);
        close(group_fd);
        close(container_fd);
        return EXIT_FAILURE;
    }

    std::println("[INFO] Controller is disabled.");

    //::ioctl(container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_map);

    close(device_fd);
    close(group_fd);
    close(container_fd);

    return EXIT_SUCCESS;
}


// TODO:
// 1) Implement ring buffer in Queue with read/write heads.
// 2) Implement necessary functions in QueuePair: it is the main structure.
//        - think about what is the main differentiation of admin and IO queues, is it
//          simple enough to be abstracted away with AdminQueue / IO Queue both extending
//          the Queue class via CRTP?
// 3)
// 1) Create admin queues (SQ, CQ) in host memory
// 2) Rethink about buffer allocaion & DMA'ing it.
//     - separate buffers for SQ, CQ?
// 3) IOVA calculations, and IOVA <-> UVA translations
//    for the ASQB and ACQB values.
// 4) Make lifetime management more obvious (make
//    the compat layer a bit nicer and tidier)
// 5) Organize shit. Don't overcomplicate things, but
//    be aware of the performance implications.
