#pragma once

#include "os/fd.hpp"
#include "vfio/container.hpp"
#include "vfio/device.hpp"

#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include <string>
#include <unordered_map>

namespace vfio {

// A VFIO group (/dev/vfio/group id)
class VfioGroup {
public:
    VfioGroup(const std::string& group_path, VfioContainer& container);

    VfioDevice& get_device(const std::string& pci_addr);

private:
    os::UniqueFd fd_;
    std::unordered_map<std::string, VfioDevice> group_device_cache_;
};

}  // namespace vfio
