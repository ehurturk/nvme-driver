#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <vector>

namespace nvme {

struct Cid {
    using value_type = std::uint16_t;

    value_type value;

    bool operator==(const Cid&) const = default;
};

class CidAllocator {
public:
    explicit CidAllocator(Cid::value_type capacity);

    ~CidAllocator();

    // pin the allocator such that any CIDs produced by this allocator are pinned to this allocator
    CidAllocator(const CidAllocator&) = delete;
    CidAllocator& operator=(const CidAllocator&) = delete;
    CidAllocator(CidAllocator&&) = delete;
    CidAllocator& operator=(CidAllocator&&) = delete;

    Cid allocate();

    void release(Cid cid) noexcept;

private:
    std::vector<Cid::value_type>
        free_;  // FIXME: surely there is a better and more memory efficient way?
    std::uint16_t capacity_;
};

}  // namespace nvme

namespace std {
template<>
struct hash<nvme::Cid> {
    size_t operator()(const nvme::Cid& cid) const noexcept {
        return hash<nvme::Cid::value_type>{}(cid.value);
    }
};

template<>
struct formatter<nvme::Cid> : formatter<nvme::Cid::value_type> {
    auto format(const nvme::Cid& cid, format_context& ctx) const {
        return formatter<nvme::Cid::value_type>::format(cid.value, ctx);
    }
};
}  // namespace std
