#include "nvme/cid.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nvme {

Cid::Cid(Cid&& other) noexcept : cid_{other.cid_}, deleter_{std::move(other.deleter_)} {
    other.cid_ = std::numeric_limits<value_type>::max();
    other.deleter_ = nullptr;
}

Cid& Cid::operator=(Cid&& other) noexcept {
    if (this != &other) {
        reset();
        cid_ = other.cid_;
        deleter_ = std::exchange(other.deleter_, nullptr);
    }

    return *this;
}

Cid::~Cid() {
    if (deleter_) {
        deleter_(cid_);
    }
}

Cid::value_type Cid::get() const {
    return cid_;
}

Cid::operator int() const {
    return cid_;
}

void Cid::reset() {
    if (deleter_) {
        deleter_(cid_);
    }
    deleter_ = nullptr;
}

CidAllocator::CidAllocator(Cid::value_type capacity) : capacity_{capacity} {
    if (capacity == 0 || capacity == 0xFFFF) {
        throw std::invalid_argument("Invalid CID capacity");
    }

    free_.reserve(capacity);

    for (std::uint32_t cid = capacity; cid > 0; cid--) {
        free_.push_back(static_cast<Cid::value_type>(cid - 1));
    }
}

CidAllocator::~CidAllocator() {
    assert(free_.size() == capacity_ && "A CID has outlived its allocator");
}

Cid CidAllocator::allocate() {
    assert(!free_.empty() && "No available CIDs");

    const auto cid = free_.back();
    free_.pop_back();
    return Cid{std::bind_front(&CidAllocator::release, this), cid};
}

void CidAllocator::release(Cid::value_type cid) noexcept {
    assert(free_.size() < capacity_ && "Max capacity reached for CIDs");
    free_.push_back(cid);
}

}  // namespace nvme
