#pragma once

#include <cerrno>
#include <system_error>

namespace vfio {

void check(bool failed, const char* what);

}  // namespace vfio
