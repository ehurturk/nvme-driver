#pragma once

#include <cstdint>
#include <format>

namespace nvme {

struct Cid {
    using value_type = std::uint16_t;

    value_type value;

    bool operator==(const Cid&) const = default;
};
}  // namespace nvme

namespace std {
template<>
struct formatter<nvme::Cid> : formatter<nvme::Cid::value_type> {
    auto format(const nvme::Cid& cid, format_context& ctx) const {
        return formatter<nvme::Cid::value_type>::format(cid.value, ctx);
    }
};
}  // namespace std
