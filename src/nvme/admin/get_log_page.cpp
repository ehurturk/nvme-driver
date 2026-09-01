#include "nvme/admin/get_log_page.hpp"

namespace nvme::admin {

namespace {
namespace smart_field {
constexpr std::size_t CRITICAL_WARNING = 0;
constexpr std::size_t COMPOSITE_TEMPERATURE = 1;
constexpr std::size_t AVAILABLE_SPARE = 3;
constexpr std::size_t PERCENTAGE_USED = 5;
constexpr std::size_t DATA_UNITS_READ = 32;
constexpr std::size_t DATA_UNITS_WRITTEN = 48;
constexpr std::size_t POWER_CYCLES = 112;
constexpr std::size_t POWER_ON_HOURS = 128;
constexpr std::size_t UNSAFE_SHUTDOWNS = 144;
constexpr std::size_t MEDIA_ERRORS = 160;
constexpr std::size_t NUM_ERROR_LOG_ENTRIES = 176;
}  // namespace smart_field

namespace errlog_field {
constexpr std::size_t ERROR_COUNT = 0;
constexpr std::size_t SQID = 8;
constexpr std::size_t CID = 10;
constexpr std::size_t STATUS = 12;
constexpr std::size_t PARAMETER_ERROR_LOCATION = 14;
constexpr std::size_t LBA = 16;
constexpr std::size_t NSID = 24;
}  // namespace errlog_field
}  // namespace

SmartLogData::SmartLogData(const std::byte* src) {
    std::memcpy(data_.data(), src, SIZE);
}

std::uint8_t SmartLogData::critical_warning() const {
    return read<std::uint8_t>(smart_field::CRITICAL_WARNING);
}

std::uint16_t SmartLogData::composite_temperature_kelvin() const {
    return read<std::uint16_t>(smart_field::COMPOSITE_TEMPERATURE);
}

std::uint8_t SmartLogData::available_spare() const {
    return read<std::uint8_t>(smart_field::AVAILABLE_SPARE);
}

std::uint8_t SmartLogData::percentage_used() const {
    return read<std::uint8_t>(smart_field::PERCENTAGE_USED);
}

std::uint64_t SmartLogData::data_units_read() const {
    return read<std::uint64_t>(smart_field::DATA_UNITS_READ);
}

std::uint64_t SmartLogData::data_units_written() const {
    return read<std::uint64_t>(smart_field::DATA_UNITS_WRITTEN);
}

std::uint64_t SmartLogData::power_cycles() const {
    return read<std::uint64_t>(smart_field::POWER_CYCLES);
}

std::uint64_t SmartLogData::power_on_hours() const {
    return read<std::uint64_t>(smart_field::POWER_ON_HOURS);
}

std::uint64_t SmartLogData::unsafe_shutdowns() const {
    return read<std::uint64_t>(smart_field::UNSAFE_SHUTDOWNS);
}

std::uint64_t SmartLogData::media_errors() const {
    return read<std::uint64_t>(smart_field::MEDIA_ERRORS);
}

std::uint64_t SmartLogData::num_error_log_entries() const {
    return read<std::uint64_t>(smart_field::NUM_ERROR_LOG_ENTRIES);
}

ErrorLogEntry::ErrorLogEntry(const std::byte* src) {
    std::memcpy(data_.data(), src, SIZE);
}

std::uint64_t ErrorLogEntry::error_count() const {
    return read<std::uint64_t>(errlog_field::ERROR_COUNT);
}

std::uint16_t ErrorLogEntry::sqid() const {
    return read<std::uint16_t>(errlog_field::SQID);
}

std::uint16_t ErrorLogEntry::cid() const {
    return read<std::uint16_t>(errlog_field::CID);
}

std::uint16_t ErrorLogEntry::status() const {
    return read<std::uint16_t>(errlog_field::STATUS);
}

std::uint16_t ErrorLogEntry::parameter_error_location() const {
    return read<std::uint16_t>(errlog_field::PARAMETER_ERROR_LOCATION);
}

std::uint64_t ErrorLogEntry::lba() const {
    return read<std::uint64_t>(errlog_field::LBA);
}

std::uint32_t ErrorLogEntry::nsid() const {
    return read<std::uint32_t>(errlog_field::NSID);
}

namespace commands {

wire::SubmissionEntry GetLogPage::build(std::uint16_t this_cid) const noexcept {
    auto c = build_base(Opcode::GetLogPage, this_cid);

    const std::uint32_t numd = utils::to_zero_based(num_dwords);

    c.nsid = nsid;
    c.dptr = wire::DataPointer::prp(prp1);
    c.cdw10 = utils::DwordBuilder<>{}
                  .set<LID>(utils::to_underlying(lid))
                  .set<RAE>(retain_async_event)
                  .set<NUMDL>(numd & 0xFFFF)
                  .get();
    c.cdw11 = utils::DwordBuilder<>{}.set<NUMDU>(numd >> 16).get();
    c.cdw12 = static_cast<DWRD>(offset & 0xFFFFFFFF);
    c.cdw13 = static_cast<DWRD>(offset >> 32);

    return c;
}

GetLogPage::Completion GetLogPage::decode_completion(const wire::CompletionEntry&) {
    return Completion{};
}

}  // namespace commands

}  // namespace nvme::admin
