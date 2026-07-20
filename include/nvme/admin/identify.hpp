#pragma once

#include <array>
#include <cstring>
#include <string_view>

#include "commands.hpp"

namespace nvme::admin
{

// controller namespace
enum class IdentifyCns : std::uint8_t
{
    Namespace          = 0x00, // NSID: Y | CNTID: N | CSI: N
    Controller         = 0x01, // NSID: N | CNTID: N | CSI: N
    ActiveNamespaceIds = 0x02, // NSID: Y | CNTID: N | CSI: N
    NSIDList           = 0x03, // NSID: Y | CNTID: N | CSI: N
    NVMSetList         = 0x04, // NSID: N | CNTID: N | CSI: N
                               // TODO: add more as required from Figure 326: Identify - CNS Values (spec page 343)
};

class IdentifyControllerData;
class IdentifyNamespaceData;

struct IdentifyController
{
    using Data                            = IdentifyControllerData;
    static constexpr auto          cns    = IdentifyCns::Controller;
    static constexpr std::uint16_t cntid  = 0;
    static constexpr std::uint16_t cnssid = 0;
};

struct IdentifyNamespace
{
    using Data                            = IdentifyNamespaceData;
    static constexpr auto          cns    = IdentifyCns::Namespace;
    static constexpr std::uint16_t cntid  = 0;
    static constexpr std::uint16_t cnssid = 0;
    // TODO: what about NSID?
};

// Identify Controller data
// NOTE: only the fields thjat I have used are modelled, add more if needed
class IdentifyControllerData
{
  public:
    static constexpr std::size_t SIZE = 4096;

    explicit IdentifyControllerData(const std::byte* src)
    {
        std::memcpy(data_.data(), src, SIZE);
    }

    std::uint16_t vid() const
    {
        return read<std::uint16_t>(0);
    }
    std::string_view serial() const
    {
        return str(4, 20);
    }
    std::string_view model() const
    {
        return str(24, 40);
    }
    std::uint8_t mdts() const
    {
        return read<std::uint8_t>(77);
    }
    std::uint16_t cntlid() const
    {
        return read<std::uint16_t>(78);
    }

    // SQES/CQES: bits [3:0] required minimum, [7:4] maximum, as 2^n,
    // these are what CC.IOSQES/IOCQES must be re-configured from
    std::uint8_t sqes() const
    {
        return read<std::uint8_t>(512);
    }
    std::uint8_t cqes() const
    {
        return read<std::uint8_t>(513);
    }

    std::uint32_t nn() const
    {
        return read<std::uint32_t>(516);
    } // no. namespaces

  private:
    template <typename T> T read(std::size_t offset) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        T v;
        std::memcpy(&v, data_.data() + offset, sizeof(T));
        return v;
    }

    std::string_view str(std::size_t offset, std::size_t len) const
    {
        if (offset > data_.size() || len > data_.size() - offset)
            return "";

        return {reinterpret_cast<const char*>(data_.data() + offset), len};
    }

    alignas(8) std::array<std::byte, SIZE> data_; // 4 KB buffer
};

class IdentifyNamespaceData
{};

} // namespace nvme::admin
