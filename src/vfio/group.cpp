#include "vfio/group.hpp"

#include "os/fd.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"
#include "vfio/error.hpp"

#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include <string>
#include <utility>

namespace vfio {

VfioGroup::VfioGroup(const std::string& group_path, VfioContainer& container) {
    fd_ = os::UniqueFd{::open(group_path.c_str(), O_RDWR)};
    check(!fd_.valid(), "open vfio group");

    vfio_group_status status{};
    status.argsz = sizeof(status);
    // check if its status can be retrieved
    check(::ioctl(fd_.get(), VFIO_GROUP_GET_STATUS, &status) != 0, "VFIO_GROUP_GET_STATUS");
    // check if group is viable
    check(
        (status.flags & VFIO_GROUP_FLAGS_VIABLE) == 0u,
        "group not viable (devices not all bound to vfio-pci)"
    );

    // attach group to the container
    int cfd = container.fd();
    check(::ioctl(fd_.get(), VFIO_GROUP_SET_CONTAINER, &cfd) != 0, "VFIO_GROUP_SET_CONTAINER");

    // enable type1 iommu only after gruop is attached
    container.enable_type1_iommu();
}

VfioDevice& VfioGroup::get_device(const std::string& pci_addr) {
    if (auto it = group_device_cache_.find(pci_addr); it != group_device_cache_.end()) {
        return it->second;
    }

    os::UniqueFd device_fd{::ioctl(fd_.get(), VFIO_GROUP_GET_DEVICE_FD, pci_addr.c_str())};
    check(!device_fd.valid(), "VFIO_GROUP_GET_DEVICE_FD");

    vfio_device_info info{};
    info.argsz = sizeof(info);

    check(::ioctl(device_fd.get(), VFIO_DEVICE_GET_INFO, &info) != 0, "VFIO_DEVICE_GET_INFO");
    // NOTE: for now I aim for PCI devices as NVME is a PCIE device in my QEMU setup, but this
    // may change?
    check((info.flags & VFIO_DEVICE_FLAGS_PCI) == 0u, "not a PCI device");
    check(info.num_regions <= VFIO_PCI_CONFIG_REGION_INDEX, "incorrect BAR regions");

    auto [device_it, inserted] = group_device_cache_.try_emplace(pci_addr, std::move(device_fd));
    device_it->second.enable_bus_master(); // enable bus master for the device to make it able to handle DMA

    return device_it->second;
}

}  // namespace vfio
