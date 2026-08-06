#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nvme
{
class CidAllocator;

class Cid
{
public:
    using value_type = std::uint16_t;

    Cid(const Cid&)            = delete;
    Cid& operator=(const Cid&) = delete;

    Cid(Cid&& other) noexcept : cid_{other.cid_}, allocator_{std::exchange(other.allocator_, nullptr)}
    {}

    Cid& operator=(Cid&& other)
    {
        if (this != &other) {
            reset();
            allocator_ = std::exchange(other.allocator_, nullptr);
            cid_       = other.cid_;
        }

        return *this;
    }

    ~Cid()
    {
        deleter_(cid_);
    }

    value_type get() const
    {
        return cid_;
    }

    operator int() const
    {
        return cid_;
    }

  private:
    friend class CidAllocator;
    // a CID should really be constructable from a CID allocator
    template <typename Fn>
    Cid(Fn&& deleter, value_type cid) : cid_{cid}, deleter_{std::forward<decltype(deleter)>(deleter)}
    {}

    void reset();

    value_type cid_;

    std::function<void(std::uint16_t)> deleter_;
    CidAllocator*                      allocator_;
};

class CidAllocator
{
  public:
    explicit CidAllocator(Cid::value_type capacity) : capacity_{capacity}
    {
        if (capacity == 0 || capacity == 0xFFFF) {
            throw std::invalid_argument("Invalid CID capacity");
        }

        free_.reserve(capacity);

        for (std::uint32_t cid = capacity; cid > 0; cid--) {
            free_.push_back(static_cast<Cid::value_type>(cid - 1));
        }
    }

    ~CidAllocator()
    {
        assert(free_.size() == capacity_ && "A CID has outlived its allocator");
    }

    // pin the allocator such that any CIDs produced by this allocator are pinned to this allocator
    CidAllocator(const CidAllocator&)            = delete;
    CidAllocator& operator=(const CidAllocator&) = delete;
    CidAllocator(CidAllocator&&)                 = delete;
    CidAllocator& operator=(CidAllocator&&)      = delete;

    Cid allocate()
    {
        assert(free_.empty() && "No available CIDs");

        const auto cid = free_.back();
        free_.pop_back();
        return Cid{std::bind_front(&CidAllocator::release, this), cid};
    }

    void release(Cid::value_type cid) noexcept
    {
        assert(free_.size() == 0xFFFF && "Max capacity reached for CIDs");
        free_.push_back(cid);
    }

  private:
    std::vector<Cid::value_type> free_;
    std::uint16_t              capacity_;
};

inline void Cid::reset()
{
    if (allocator_)
        allocator_->release(cid_);
    allocator_ = nullptr;
}

} // namespace nvme
