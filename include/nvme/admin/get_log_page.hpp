#pragma once

#include "dma/buffer.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace nvme::admin {

enum class LogPageId : std::uint8_t {
    Error = 0x01,
    SmartHealth = 0x02,
    FirmwareSlot = 0x03,
    ChangedNamespaceList = 0x04,
    CommandsSupportedAndEffects = 0x05,
};

class SmartLogData {
public:
    static constexpr std::size_t SIZE = 512;

    explicit SmartLogData(const std::byte* src);

    std::uint8_t critical_warning() const;

    std::uint16_t composite_temperature_kelvin() const;

    std::uint8_t available_spare() const;

    std::uint8_t percentage_used() const;

    std::uint64_t data_units_read() const;

    std::uint64_t data_units_written() const;

    std::uint64_t power_cycles() const;

    std::uint64_t power_on_hours() const;

    std::uint64_t unsafe_shutdowns() const;

    std::uint64_t media_errors() const;

    std::uint64_t num_error_log_entries() const;

private:
    template<typename T>
    T read(std::size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T v;
        std::memcpy(&v, data_.data() + offset, sizeof(T));
        return v;
    }

    alignas(8) std::array<std::byte, SIZE> data_;
};

class ErrorLogEntry {
public:
    static constexpr std::size_t SIZE = 64;

    explicit ErrorLogEntry(const std::byte* src);

    std::uint64_t error_count() const;

    std::uint16_t sqid() const;

    std::uint16_t cid() const;

    std::uint16_t status() const;

    std::uint16_t parameter_error_location() const;

    std::uint64_t lba() const;

    std::uint32_t nsid() const;

private:
    template<typename T>
    T read(std::size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T v;
        std::memcpy(&v, data_.data() + offset, sizeof(T));
        return v;
    }

    alignas(8) std::array<std::byte, SIZE> data_;
};

namespace commands {

class GetLogPage {
public:
    static constexpr std::string_view name = "Get Log Page";

    using Data = std::vector<std::byte>;

    GetLogPage(
        LogPageId lid,
        std::uint32_t num_dwords,
        std::uint32_t nsid = 0xFFFFFFFF,
        bool retain_async_event = false,
        std::uint64_t offset = 0
    )
        : lid{lid}, num_dwords{num_dwords}, nsid{nsid}, retain_async_event{retain_async_event}, offset{offset} {
    }

    std::size_t data_size() const;

    wire::SubmissionEntry build(std::uint16_t this_cid, dma::DmaView data) const noexcept;

    Data decode(std::span<const std::byte> bytes) const;

private:
    using LID = utils::BitField<DWRD, 0, 8>;
    using RAE = utils::BitField<DWRD, 15, 1>;
    using NUMDL = utils::BitField<DWRD, 16, 16>;
    using NUMDU = utils::BitField<DWRD, 0, 16>;

    LogPageId lid;
    std::uint32_t num_dwords;
    std::uint32_t nsid;
    bool retain_async_event;
    std::uint64_t offset;
};

static_assert(DataCommand<GetLogPage>);

}  // namespace commands

}  // namespace nvme::admin
