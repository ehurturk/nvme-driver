#pragma once

#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace nvme::admin {

// controller namespace
enum class IdentifyCns : std::uint8_t {
    Namespace = 0x00,           // NSID: Y | CNTID: N | CSI: N
    Controller = 0x01,          // NSID: N | CNTID: N | CSI: N
    ActiveNamespaceIds = 0x02,  // NSID: Y | CNTID: N | CSI: N
    NSIDList = 0x03,            // NSID: Y | CNTID: N | CSI: N
    NVMSetList =
        0x04,  // NSID: N | CNTID: N | CSI: N
               // TODO: add more as required from Figure 326: Identify - CNS Values (spec page 343)
};

class IdentifyControllerData;
class IdentifyNamespaceData;

struct IdentifyController {
    using Data = IdentifyControllerData;
    static constexpr auto cns = IdentifyCns::Controller;
    static constexpr std::uint16_t cntid = 0;
    static constexpr std::uint16_t cnssid = 0;
};

struct IdentifyNamespace {
    using Data = IdentifyNamespaceData;
    static constexpr auto cns = IdentifyCns::Namespace;
    static constexpr std::uint16_t cntid = 0;
    static constexpr std::uint16_t cnssid = 0;
    // TODO: what about NSID?
};

// Identify Controller data
// NOTE: only the fields thjat I have used are modelled, add more if needed
class IdentifyControllerData {
public:
    static constexpr std::size_t SIZE = 4096;

    explicit IdentifyControllerData(const std::byte* src);

    std::uint16_t vid() const;

    std::string_view serial() const;

    std::string_view model() const;

    std::uint8_t mdts() const;

    std::uint16_t cntlid() const;

    // SQES/CQES: bits [3:0] required minimum, [7:4] maximum, as 2^n,
    // these are what CC.IOSQES/IOCQES must be re-configured from
    std::uint8_t sqes() const;

    std::uint8_t cqes() const;

    std::uint32_t nn() const;  // no. namespaces

private:
    template<typename T>
    T read(std::size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T v;
        std::memcpy(&v, data_.data() + offset, sizeof(T));
        return v;
    }

    std::string_view str(std::size_t offset, std::size_t len) const;

    alignas(8) std::array<std::byte, SIZE> data_;  // 4 KB buffer
};

class IdentifyNamespaceData {};

template<typename T, typename Expected>
concept SameAsUnqualified = std::same_as<std::remove_cvref_t<T>, Expected>;

template<typename Target>
concept IsIdentifyTarget = requires {
    typename Target::Data;
    { Target::cns } -> std::convertible_to<IdentifyCns>;
    { Target::cntid } -> SameAsUnqualified<std::uint16_t>;
    { Target::cnssid } -> SameAsUnqualified<std::uint16_t>;
};

namespace commands {

// Identify (opcode 0x06)
// DPTR (128 bits): start of the data buffer
// CDW10: bits 0 - 7 CNS: Controller or namespace structure
//            16 - 31 CNTID: Controller Identifier
// CDW11: bits 0 - 15: CNSSID (CNS Specific Identifier): Dependent on the specified CNS value
//            24 - 31: CSI: Command Set Identifier (if not used should be cleared to 0x0).
template<IsIdentifyTarget Target>
class Identify {
public:
    using Data = typename Target::Data;
    enum class Csi : std::uint8_t {
        NVM = 0x0,
        KeyValue = 0x1,
        ZonedNamespace = 0x2,
        SubsystemLocalMemory = 0x3,
        ComputationalPrograms = 0x4
    };

    // TODO: based on the CNS set NSID, CNTID, CSI automatically
    CommonCommand build(std::uint16_t this_cid) const noexcept {
        // set CDW0.PSDT = 0x0 to indicate the use of PRPs for the transfer
        CommonCommand c =
            CommonCommand::build(Opcode::Identify, this_cid, CommonCommand::Psdt::Prp);

        c.nsid = nsid;
        // use only prp1 as a 4 KB buffer fits exactly into a page, so no need to use prp2
        c.dptr = DataPointer::prp(prp1);
        c.cdw10 = utils::DwordBuilder<>{}
                      .set<CNS>(utils::to_underlying(cns))
                      .template set<CNTID>(cntid)
                      .get();
        c.cdw11 = utils::DwordBuilder<>{}
                      .set<CNSSID>(cnssid)
                      .template set<CSI>(utils::to_underlying(csi))
                      .get();

        return c;
    }

    // no need to look at Completion, as the controller DMAs the resulting data buffer that
    // describes information about the NVM subsystem, the domain,  the controller or the namespaces
    // to the DPTR. the data structure is 4096 bytes in size
    struct Completion {};

    static Completion decode_completion(const CompletionQueueEntry&) {
        return Completion{};
    }

public:
    std::uint64_t prp1{};    // iova of 4 KiB result buffer
    std::uint32_t nsid = 0;  // only for namespace scoped cns values
    Csi csi = Csi::NVM;

private:
    using CNS = utils::BitField<DWRD, 0, 8>;
    using CNTID = utils::BitField<DWRD, 16, 16>;
    using CNSSID = utils::BitField<DWRD, 0, 16>;
    using CSI = utils::BitField<DWRD, 24, 8>;

private:
    static constexpr auto cns = Target::cns;
    static constexpr auto cntid = Target::cntid;
    static constexpr auto cnssid = Target::cnssid;
};

using IdentifyControllerType = Identify<IdentifyController>;
using IdentifyNamespaceType = Identify<IdentifyNamespace>;

static_assert(CommandDescriptor<IdentifyControllerType>);

}  // namespace commands

}  // namespace nvme::admin
