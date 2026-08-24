#include "os/fd.hpp"

#include <unistd.h>

#include <utility>

namespace os {

UniqueFd::UniqueFd(int fd) : fd_{fd} {
}

UniqueFd::~UniqueFd() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

UniqueFd::UniqueFd(UniqueFd&& o) noexcept : fd_{std::exchange(o.fd_, nfd)} {
}

UniqueFd& UniqueFd::operator=(UniqueFd&& o) noexcept {
    if (this != &o) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = std::exchange(o.fd_, nfd);
    }
    return *this;
}

int UniqueFd::get() const {
    return fd_;
}

bool UniqueFd::valid() const {
    return fd_ >= 0;
}

}  // namespace os
