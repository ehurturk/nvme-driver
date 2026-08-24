#pragma once

#include "os/fd.hpp"
#include "utils.hpp"
#include "vfio/error.hpp"

#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include <cstddef>
#include <cstdint>
#include <print>
#include <stdexcept>
#include <vector>

namespace vfio {

class VfioContainer {
public:
    VfioContainer();

    ~VfioContainer() = default;

    VfioContainer(const VfioContainer&) = delete;
    VfioContainer& operator=(const VfioContainer&) = delete;

    VfioContainer(VfioContainer&&) = default;
    VfioContainer& operator=(VfioContainer&&) = default;

    int fd() const;

    // VfioGroup must call this on attach to the container
    void enable_type1_iommu();

    // Create an IOMMU mapping for the given memory region.
    // NOTE: Expects that uva points to page-aligned MAP_SHARED | MAP_ANONYMOUS memory.
    std::uint64_t map_dma(void* uva, std::size_t size);

    void unmap_dma(std::uint64_t iova, std::size_t size);

private:
    static constexpr std::size_t PAGE_SIZE = 4096;

    struct IovaRange {
        std::uint64_t first;
        std::uint64_t last;  // inclusive
    };

    // Note from linux/vfio.h:
    //    "Any DMA map attempt outside the valid iova range will return error."
    void cache_iova_ranges();

    bool iova_range_valid(std::uint64_t base, std::size_t size) const;

    void seed_iova_cursor();

    std::uint64_t alloc_iova(std::size_t size);

private:
    os::UniqueFd fd_;
    std::vector<IovaRange> iova_ranges_;
    std::uint64_t iova_cursor_ = 0x10000;
    bool iommu_enabled_ = false;
};

}  // namespace vfio
