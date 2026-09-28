#include "nvme/cid.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace nvme {

CidAllocator::CidAllocator(Cid::value_type capacity) : capacity_{capacity} {
    if (capacity == 0 || capacity == 0xFFFF) {
        throw std::invalid_argument("Invalid CID capacity");
    }

    free_.reserve(capacity);

    for (std::uint32_t cid = capacity; cid > 0; cid--) {
        free_.push_back(static_cast<Cid::value_type>(cid - 1));
    }
}

std::optional<Cid> CidAllocator::allocate() {
    if (free_.empty()) {
        return std::nullopt;
    }

    const auto cid = free_.back();
    free_.pop_back();
    return Cid{cid};
}

void CidAllocator::release(Cid cid) noexcept {
    assert(free_.size() < capacity_ && "Max capacity reached for CIDs");
    free_.push_back(cid.value);
}

}  // namespace nvme
