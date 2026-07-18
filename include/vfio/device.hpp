#pragma once

#include <linux/vfio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cstdint>
#include <utility>

#include "os/fd.hpp"
#include "os/mmap.hpp"
#include "vfio/error.hpp"

namespace vfio
{

// A device inside a VFIO group
class VfioDevice
{
  public:
    explicit VfioDevice(os::UniqueFd fd) : fd_{std::move(fd)}
    {
        vfio_device_info info{};
        info.argsz = sizeof(info);

        check(::ioctl(fd_.get(), VFIO_DEVICE_GET_INFO, &info) != 0, "VFIO_DEVICE_GET_INFO");
        // NOTE: for now I aim for PCI devices as NVME is a PCIE device in my QEMU setup, but this may change?
        check(!(info.flags & VFIO_DEVICE_FLAGS_PCI), "not a PCI device");
        check(info.num_regions < 1, "no BAR regions");
    }

    vfio_region_info region_info(std::uint32_t index) const
    {
        vfio_region_info info{};
        info.argsz = sizeof(info);
        info.index = index;

        check(::ioctl(fd_.get(), VFIO_DEVICE_GET_REGION_INFO, &info) != 0, "VFIO_DEVICE_GET_REGION_INFO");

        return info;
    }

    // mmap a device region into host address space
    // bar0 is VFIO_PCI_BAR0_REGION_INDEX
    os::MappedMemory map_region(std::uint32_t index) const
    {
        const vfio_region_info info = region_info(index);

        check(!(info.flags & VFIO_REGION_INFO_FLAG_MMAP), "device region does not support mmap");

        // NOTE: this really shouldn't use MAP_ANONYMOUS as this is a file backed mmap, and the backing file
        //       of the pages are the pages of the device file.
        return os::MappedMemory{info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_.get(),
                                static_cast<off_t>(info.offset)};
    }

    void reset()
    {
        check(::ioctl(fd_.get(), VFIO_DEVICE_RESET) != 0, "VFIO_DEVICE_RESET");
    }

    int fd() const
    {
        return fd_.get();
    }

  private:
    os::UniqueFd fd_;
};

} // namespace vfio
