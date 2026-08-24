#pragma once

#include <unistd.h>

#include <utility>

namespace os {

// RAII on fds
class UniqueFd {
public:
    static constexpr int nfd{-1};

    UniqueFd() = default;

    explicit UniqueFd(int fd);

    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& o) noexcept;

    UniqueFd& operator=(UniqueFd&& o) noexcept;

    int get() const;

    bool valid() const;

private:
    int fd_{nfd};
};

}  // namespace os
