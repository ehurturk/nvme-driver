#include "nvme/admin/identify.hpp"

namespace nvme::admin {

IdentifyControllerData::IdentifyControllerData(const std::byte* src) {
    std::memcpy(data_.data(), src, SIZE);
}

std::uint16_t IdentifyControllerData::vid() const {
    return read<std::uint16_t>(0);
}

std::string_view IdentifyControllerData::serial() const {
    return str(4, 20);
}

std::string_view IdentifyControllerData::model() const {
    return str(24, 40);
}

std::uint8_t IdentifyControllerData::mdts() const {
    return read<std::uint8_t>(77);
}

std::uint16_t IdentifyControllerData::cntlid() const {
    return read<std::uint16_t>(78);
}

std::uint8_t IdentifyControllerData::sqes() const {
    return read<std::uint8_t>(512);
}

std::uint8_t IdentifyControllerData::cqes() const {
    return read<std::uint8_t>(513);
}

std::uint32_t IdentifyControllerData::nn() const {
    return read<std::uint32_t>(516);
}

std::string_view IdentifyControllerData::str(std::size_t offset, std::size_t len) const {
    if (offset > data_.size() || len > data_.size() - offset) {
        return "";
    }

    return {reinterpret_cast<const char*>(data_.data() + offset), len};
}

}  // namespace nvme::admin
