#pragma once

#include <unistd.h>

#include <utility>

namespace os {

// RAII on fds
class UniqueFd {
public:
    static constexpr int nfd{-1};

    UniqueFd() = default;

    explicit UniqueFd(int fd) : fd_{fd} {
    }

    ~UniqueFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& o) noexcept : fd_{std::exchange(o.fd_, nfd)} {
    }

    UniqueFd& operator=(UniqueFd&& o) noexcept {
        if (this != &o) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = std::exchange(o.fd_, nfd);
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

    bool valid() const {
        return fd_ >= 0;
    }

private:
    int fd_{nfd};
};

}  // namespace os
