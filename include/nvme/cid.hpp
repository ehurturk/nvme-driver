#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <print>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nvme {
class CidAllocator;

class Cid {
public:
    using value_type = std::uint16_t;

    Cid(const Cid&) = delete;
    Cid& operator=(const Cid&) = delete;

    Cid(Cid&& other) noexcept;

    Cid& operator=(Cid&& other) noexcept;

    ~Cid();

    value_type get() const;

    operator int() const;

private:
    friend class CidAllocator;

    // a CID should really be constructable from a CID allocator
    template<typename Fn>
    Cid(Fn&& deleter, value_type cid)
        : cid_{cid}, deleter_{std::forward<decltype(deleter)>(deleter)} {
    }

    void reset();

    value_type cid_;

    std::function<void(std::uint16_t)> deleter_;
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

    void release(Cid::value_type cid) noexcept;

private:
    std::vector<Cid::value_type>
        free_;  // FIXME: surely there is a better and more memory efficient way?
    std::uint16_t capacity_;
};

}  // namespace nvme
