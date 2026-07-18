#pragma once

#include <cerrno>
#include <system_error>

namespace vfio
{

inline void check(bool failed, const char* what)
{
    if (failed)
        throw std::system_error(errno, std::generic_category(), what);
}

} // namespace vfio
