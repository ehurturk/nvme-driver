#pragma once

#include "nvme_spec.hpp"
namespace nvme
{

class Controller
{
    public:
        Controller(void *bar0);

    private:
        volatile NvmeBar0 ctl_space_;
};

} // namespace nvme
