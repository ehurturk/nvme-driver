#pragma once

#include "os/fd.hpp"
#include "os/mmap.hpp"

#include <linux/pci_regs.h>  // for PCI_COMMAND, PCI_COMMAND_MEMORY, PCI_COMMAND_MASTER
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>

namespace vfio {

// A device inside a VFIO group
class VfioDevice {
public:
    explicit VfioDevice(os::UniqueFd fd);

    vfio_region_info region_info(std::uint32_t index) const;

    // mmap a device region into host address space
    // bar0 is VFIO_PCI_BAR0_REGION_INDEX
    os::MappedMemory map_region(std::uint32_t index) const;

    // NOTE: On QEMU, I have noticed that I was getting CSTS=0x2 after ringing doorbells, so there
    // was an issue with DMA. After further investigation, I have found out that even though device
    // is bound to the vfio-pci driver, its Bus Master Enable bit in the Command register of the
    // VFIO_PCI_CONFIG_REGION_INDEX was not set to ON, hence the DMA was not set up correctly. To
    // turn the bit ON, we have to manually flip the bit on via accessing the region.
    void enable_bus_master();

    void reset();

    int fd() const;

private:
    os::UniqueFd fd_;
};

}  // namespace vfio
