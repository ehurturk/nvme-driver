#pragma once

#include "fd.hpp"

namespace vfio
{
class VfioDevice
{
  public:
    inline int fd() const
    {
        return device_fd_.get();
    }

  private:
    UniqueFd device_fd_;
};

} // namespace vfio
