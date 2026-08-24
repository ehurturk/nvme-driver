#include "nvme/admin/identify.hpp"

namespace nvme::admin {

namespace {
namespace ctrl_field {
constexpr std::size_t VID = 0;
constexpr std::size_t SN = 4;
constexpr std::size_t SN_LEN = 20;
constexpr std::size_t MN = 24;
constexpr std::size_t MN_LEN = 40;
constexpr std::size_t MDTS = 77;
constexpr std::size_t CNTLID = 78;
constexpr std::size_t SQES = 512;
constexpr std::size_t CQES = 513;
constexpr std::size_t NN = 516;
}  // namespace ctrl_field

namespace ns_field {
constexpr std::size_t NSZE = 0;
constexpr std::size_t NCAP = 8;
constexpr std::size_t NUSE = 16;
constexpr std::size_t NLBAF = 25;
constexpr std::size_t FLBAS = 26;
constexpr std::size_t LBAF_TABLE = 128;
constexpr std::size_t LBAF_STRIDE = 4;
}  // namespace ns_field

using FLBAS_FORMAT_INDEX = utils::BitField<std::uint8_t, 0, 4>;
using LBAF_MS = utils::BitField<std::uint32_t, 0, 16>;
using LBAF_LBADS = utils::BitField<std::uint32_t, 16, 8>;
using LBAF_RP = utils::BitField<std::uint32_t, 24, 2>;
}  // namespace

IdentifyControllerData::IdentifyControllerData(const std::byte* src) {
    std::memcpy(data_.data(), src, SIZE);
}

std::uint16_t IdentifyControllerData::vid() const {
    return read<std::uint16_t>(ctrl_field::VID);
}

std::string_view IdentifyControllerData::serial() const {
    return str(ctrl_field::SN, ctrl_field::SN_LEN);
}

std::string_view IdentifyControllerData::model() const {
    return str(ctrl_field::MN, ctrl_field::MN_LEN);
}

std::uint8_t IdentifyControllerData::mdts() const {
    return read<std::uint8_t>(ctrl_field::MDTS);
}

std::uint16_t IdentifyControllerData::cntlid() const {
    return read<std::uint16_t>(ctrl_field::CNTLID);
}

std::uint8_t IdentifyControllerData::sqes() const {
    return read<std::uint8_t>(ctrl_field::SQES);
}

std::uint8_t IdentifyControllerData::cqes() const {
    return read<std::uint8_t>(ctrl_field::CQES);
}

std::uint32_t IdentifyControllerData::nn() const {
    return read<std::uint32_t>(ctrl_field::NN);
}

std::string_view IdentifyControllerData::str(std::size_t offset, std::size_t len) const {
    if (offset > data_.size() || len > data_.size() - offset) {
        return "";
    }

    return {reinterpret_cast<const char*>(data_.data() + offset), len};
}

IdentifyNamespaceData::IdentifyNamespaceData(const std::byte* src) {
    std::memcpy(data_.data(), src, SIZE);
}

std::uint64_t IdentifyNamespaceData::nsze() const {
    return read<std::uint64_t>(ns_field::NSZE);
}

std::uint64_t IdentifyNamespaceData::ncap() const {
    return read<std::uint64_t>(ns_field::NCAP);
}

std::uint64_t IdentifyNamespaceData::nuse() const {
    return read<std::uint64_t>(ns_field::NUSE);
}

std::uint8_t IdentifyNamespaceData::nlbaf() const {
    return read<std::uint8_t>(ns_field::NLBAF);
}

std::uint8_t IdentifyNamespaceData::flbas() const {
    return read<std::uint8_t>(ns_field::FLBAS);
}

std::uint8_t IdentifyNamespaceData::active_lba_format_index() const {
    return FLBAS_FORMAT_INDEX::extract(flbas());
}

IdentifyNamespaceData::LbaFormat IdentifyNamespaceData::lba_format(std::uint8_t index) const {
    const auto raw =
        read<std::uint32_t>(ns_field::LBAF_TABLE + (std::size_t{index} * ns_field::LBAF_STRIDE));
    return LbaFormat{
        .metadata_size = static_cast<std::uint16_t>(LBAF_MS::extract(raw)),
        .lba_data_shift = static_cast<std::uint8_t>(LBAF_LBADS::extract(raw)),
        .relative_performance = static_cast<std::uint8_t>(LBAF_RP::extract(raw)),
    };
}

IdentifyNamespaceData::LbaFormat IdentifyNamespaceData::active_lba_format() const {
    return lba_format(active_lba_format_index());
}

std::uint32_t IdentifyNamespaceData::lba_size_bytes() const {
    return std::uint32_t{1} << active_lba_format().lba_data_shift;
}

}  // namespace nvme::admin
