#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace nvme
{

class CidAllocator
{
  public:
    explicit CidAllocator(std::uint16_t capacity)
    {
        if (capacity == 0 || capacity == 0xFFFF) {
            throw std::invalid_argument("Invalid CID capacity");
        }

        free_.reserve(capacity);

        for (std::uint32_t cid = capacity; cid > 0; cid--) {
            free_.push_back(static_cast<std::uint16_t>(cid - 1));
        }
    }

    ~CidAllocator() = default;

    CidAllocator(const CidAllocator&)            = delete;
    CidAllocator& operator=(const CidAllocator&) = delete;

    // TODO: in the future either use optional or expected
    std::uint16_t allocate()
    {
        if (free_.empty()) {
            throw std::runtime_error{"No available CIDs"};
        }

        const auto cid = free_.back();
        free_.pop_back();
        return cid;
    }

    void release(std::uint16_t cid)
    {
        if (free_.size() == 0xFFFF) {
            throw std::runtime_error{"Max capacity reached for CIDs"};
        }

        free_.push_back(cid);
    }

  private:
    std::vector<std::uint16_t> free_;
    std::uint16_t              next_;
};

} // namespace nvme
