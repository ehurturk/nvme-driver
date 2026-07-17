#pragma once

#include <linux/vfio.h>

#include <assert.h>
#include <cstdint>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <print>
#include <string_view>
#include <system_error>

#include "fd.hpp"
#include "lifetime.hpp"

namespace nvme
{
class VfioContainer
{
  public:
    VfioContainer(std::string_view group_path)
    {
        // create a new VFIO container
        container_fd_ = UniqueFd{::open("/dev/vfio/vfio", O_RDWR)};
        check(!container_fd_.valid(), "open /dev/vfio/vfio");

        check(::ioctl(container_fd_.get(), VFIO_GET_API_VERSION) != VFIO_API_VERSION, "unexpected VFIO API version");
        check(::ioctl(container_fd_.get(), VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) == 0, "Type1 IOMMU not supported");

        // open the group
        group_fd_ = UniqueFd{::open(group_path.data(), O_RDWR)};
        check(!group_fd_.valid(), "open vfio group");

        // Test the group is viable and available
        vfio_group_status group_status{.argsz = sizeof(group_status)};
        check(::ioctl(group_fd_.get(), VFIO_GROUP_GET_STATUS, &group_status) != 0, "VFIO_GROUP_GET_STATUS");
        check(!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE), "group not viable (devices not all bound to vfio-pci)");

        int cfd = container_fd_.get();

        // Add the group to the container
        check(::ioctl(group_fd_.get(), VFIO_GROUP_SET_CONTAINER, cfd) != 0, "VFIO_GROUP_SET_CONTAINER");

        // Enable IOMMU model we want (TYPE_1_IOMMU)
        check(::ioctl(container_fd_.get(), VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) != 0, "VFIO_SET_IOMMU Type1");

        iommu_info_ = vfio_iommu_type1_info{.argsz = sizeof(iommu_info_)};
        // Get IOMMU info, has these fields:
        //   - flags
        //   - iova_pgsizes
        //   - cap_offset
        check(::ioctl(container_fd_.get(), VFIO_IOMMU_GET_INFO, &iommu_info_) != 0, "Can't get info about IOMMU.");
    }

    ~VfioContainer() = default;

    VfioContainer(const VfioContainer&)            = delete;
    VfioContainer& operator=(const VfioContainer&) = delete;

    // fds move themselves
    VfioContainer(VfioContainer&& other)      = default;
    VfioContainer& operator=(VfioContainer&&) = default;

    // Create a IOMMU mapping for the given memory address.
    // NOTE: Expects that uva is a mmapped memory addres, with MAP_SHARED | MAP_ANOPNYMOUS flags.
    std::uint64_t map_dma(void* uva, std::size_t size)
    {
        check(reinterpret_cast<std::uintptr_t>(uva) % 4096 != 0 || size % 4096 != 0, "DMA buffer not page-aligned");

        std::uint64_t iova = alloc_iova(uva, size);

        vfio_iommu_type1_dma_map dma_map{.argsz = sizeof(dma_map)};
        dma_map.vaddr = reinterpret_cast<std::uint64_t>(uva);
        dma_map.size  = size;
        dma_map.iova  = iova;
        dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

        check(::ioctl(container_fd_.get(), VFIO_IOMMU_MAP_DMA, &dma_map) != 0, "VFIO_IOMMU_MAP_DMA");

        return iova;
    }

    void unmap_dma(std::uint64_t iova, std::size_t size)
    {
        vfio_iommu_type1_dma_unmap u{.argsz = sizeof(u)};
        u.iova = iova;
        u.size = size;

        if (::ioctl(container_fd_.get(), VFIO_IOMMU_UNMAP_DMA, &u) != 0)
            std::println("[WARN] VFIO_IOMMU_UNMAP_DMA failed for iova {:#x}", iova);
    }

    int setup_device(std::string_view pci_addr)
    {
        device_fd_ = UniqueFd{::ioctl(group_fd_.get(), VFIO_GROUP_GET_DEVICE_FD, pci_addr.data())};

        check(!device_fd_.valid(), "VFIO_GROUP_GET_DEVICE_FD");

        // Test and setup the device
        vfio_device_info device_info{.argsz = sizeof(device_info)};

        check(::ioctl(device_fd_.get(), VFIO_DEVICE_GET_INFO, &device_info) != 0, "VFIO_DEVICE_GET_INFO");
        check(!(device_info.flags & VFIO_DEVICE_FLAGS_PCI), "not a PCI device");
        check(device_info.num_regions < 1, "no BAR regions");

        return device_fd_.get();
    }

    inline int device_fd() const
    {
        return device_fd_.get();
    }


  private:
    std::uint64_t alloc_iova(void* uva, std::size_t size)
    {
        std::uint64_t iova_base = reinterpret_cast<std::uint64_t>(uva);
        if (!validate_iova(iova_base, size))
            return 0x0;
        return iova_base;
    }

    // BUG: The big one: your original validate_iova re-ran VFIO_IOMMU_GET_INFO and malloc'd on every single map_dma
    bool validate_iova(std::uint64_t iova_base, std::size_t size)
    {
        std::uintptr_t start_iova = reinterpret_cast<std::uintptr_t>(iova_base);
        std::uintptr_t end_iova   = start_iova + size - 1;

        // check alignment of the dma mapped RAM buffer
        check(start_iova % 4096 != 0 || size % 4096 != 0, "UVA/Size is not 4KB page aligned");

        // allocate argsz amount of memory to iterate over each capability header
        vfio_iommu_type1_info* info = static_cast<vfio_iommu_type1_info*>(malloc(iommu_info_.argsz));
        info->argsz                 = iommu_info_.argsz;

        // BUG: throws exception, though maybe we have to free(info)?
        check(::ioctl(container_fd_.get(), VFIO_IOMMU_GET_INFO, info) < 0, "Failed to get IOMMU info");

        if (info->flags & VFIO_IOMMU_INFO_CAPS) {
            std::byte* raw_cap_addr            = reinterpret_cast<std::byte*>(info) + info->cap_offset;
            auto*      header                  = compat::load_object_from_bytes<vfio_info_cap_header>(raw_cap_addr);
            bool       uva_fits_in_iova_ranges = false;

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
                    auto* cap = reinterpret_cast<vfio_iommu_type1_info_cap_iova_range*>(header);

                    for (int i = 0; i < cap->nr_iovas; i++) {
                        std::uint64_t range_start = cap->iova_ranges[i].start;
                        std::uint64_t range_end   = cap->iova_ranges[i].end;

                        if (start_iova >= range_start && end_iova <= range_end) {
                            // found the IOVA window, can set it
                            std::println("Found IOVA range: {} - {}. UVA is {}", range_start, range_end, iova_base);
                            uva_fits_in_iova_ranges = true;
                        }
                    }
                    break;
                }

                if (header->next) {
                    std::byte* next_addr = reinterpret_cast<std::byte*>(info) + header->next;
                    header               = compat::load_object_from_bytes<vfio_info_cap_header>(next_addr);
                } else {
                    header = nullptr;
                }
            }

            if (!uva_fits_in_iova_ranges) {
                std::println("[INFO]: Could not find a valid IOVA range that fits "
                             "the UVA. Setting DMA IOVA base address as 0x0.");
                return false;
            }
        }

        free(info);
        return true;
    }

    inline void check(bool failed, const char* what)
    {
        if (failed)
            throw std::system_error(errno, std::generic_category(), what);
    }

  private:
    UniqueFd              container_fd_;
    UniqueFd              group_fd_;
    UniqueFd              device_fd_;
    vfio_iommu_type1_info iommu_info_;
};

} // namespace nvme
