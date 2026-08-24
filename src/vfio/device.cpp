#include "vfio/device.hpp"

#include "os/fd.hpp"
#include "os/mmap.hpp"
#include "vfio/error.hpp"

#include <linux/pci_regs.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <utility>

namespace vfio {

VfioDevice::VfioDevice(os::UniqueFd fd) : fd_{std::move(fd)} {
    vfio_device_info info{};
    info.argsz = sizeof(info);

    check(::ioctl(fd_.get(), VFIO_DEVICE_GET_INFO, &info) != 0, "VFIO_DEVICE_GET_INFO");
    // NOTE: for now I aim for PCI devices as NVME is a PCIE device in my QEMU setup, but this
    // may change?
    check(!(info.flags & VFIO_DEVICE_FLAGS_PCI), "not a PCI device");
    check(info.num_regions < 1, "no BAR regions");
}

vfio_region_info VfioDevice::region_info(std::uint32_t index) const {
    vfio_region_info info{};
    info.argsz = sizeof(info);
    info.index = index;

    check(
        ::ioctl(fd_.get(), VFIO_DEVICE_GET_REGION_INFO, &info) != 0,
        "VFIO_DEVICE_GET_REGION_INFO"
    );

    return info;
}

os::MappedMemory VfioDevice::map_region(std::uint32_t index) const {
    const vfio_region_info info = region_info(index);

    check(!(info.flags & VFIO_REGION_INFO_FLAG_MMAP), "device region does not support mmap");

    // NOTE: this really shouldn't use MAP_ANONYMOUS as this is a file backed mmap, and the
    // backing file of the pages are the pages of the device file.
    return os::MappedMemory{
        info.size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd_.get(),
        static_cast<off_t>(info.offset)
    };
}

void VfioDevice::enable_bus_master() {
    const vfio_region_info info = region_info(VFIO_PCI_CONFIG_REGION_INDEX);
    const off_t cmd_off = static_cast<off_t>(info.offset) + PCI_COMMAND;

    std::uint16_t command = 0;
    check(
        ::pread(fd_.get(), &command, sizeof(command), cmd_off) != sizeof(command),
        "pread PCI_COMMAND"
    );

    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;

    check(
        ::pwrite(fd_.get(), &command, sizeof(command), cmd_off) != sizeof(command),
        "pwrite PCI_COMMAND"
    );
}

void VfioDevice::reset() {
    check(::ioctl(fd_.get(), VFIO_DEVICE_RESET) != 0, "VFIO_DEVICE_RESET");
}

int VfioDevice::fd() const {
    return fd_.get();
}

}  // namespace vfio
