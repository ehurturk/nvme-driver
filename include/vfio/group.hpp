#pragma once

#include <linux/vfio.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <string>

#include "os/fd.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"
#include "vfio/error.hpp"

namespace vfio
{

// A VFIO group (/dev/vfio/group id)
class VfioGroup
{
  public:
    VfioGroup(const std::string& group_path, VfioContainer& container)
    {
        fd_ = os::UniqueFd{::open(group_path.c_str(), O_RDWR)};
        check(!fd_.valid(), "open vfio group");

        vfio_group_status status{};
        status.argsz = sizeof(status);
        // check if its status can be retrieved
        check(::ioctl(fd_.get(), VFIO_GROUP_GET_STATUS, &status) != 0, "VFIO_GROUP_GET_STATUS");
        // check if group is viable
        check(!(status.flags & VFIO_GROUP_FLAGS_VIABLE), "group not viable (devices not all bound to vfio-pci)");

        // attach group to the container
        int cfd = container.fd();
        check(::ioctl(fd_.get(), VFIO_GROUP_SET_CONTAINER, &cfd) != 0, "VFIO_GROUP_SET_CONTAINER");

        // enable type1 iommu only after gruop is attached
        container.enable_type1_iommu();
    }

    VfioDevice get_device(const std::string& pci_addr)
    {
        os::UniqueFd device_fd{::ioctl(fd_.get(), VFIO_GROUP_GET_DEVICE_FD, pci_addr.c_str())};
        check(!device_fd.valid(), "VFIO_GROUP_GET_DEVICE_FD");

        return VfioDevice{std::move(device_fd)};
    }

  private:
    os::UniqueFd fd_;
};

} // namespace vfio
