#pragma once

#include "dma/buffer.hpp"
#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace nvme::admin {

// controller namespace
// clang-format off
enum class IdentifyCns : std::uint8_t {
    Namespace = 0x00,                             // NSID: Y | CNTID: N | CSI: N
    Controller = 0x01,                            // NSID: N | CNTID: N | CSI: N
    ActiveNamespaceIdList = 0x02,                 // NSID: Y | CNTID: N | CSI: N
    NamespaceIdentificationDescriptorList = 0x03, // NSID: Y | CNTID: N | CSI: N
    NVMSetList = 0x04,                            // NSID: N | CNTID: N | CSI: N
    // TODO: add more as required from Figure 326: Identify - CNS Values (spec page 343)
};

struct IdentifyCnsProps {
    bool nsid_used;
    bool cntid_used;
    bool csi_used;
};

inline constexpr std::array<IdentifyCnsProps, 5> identify_props {
    /* Namespace  */            IdentifyCnsProps{.nsid_used = true, .cntid_used = false, .csi_used = false},
    /* Controller */            IdentifyCnsProps{.nsid_used = false, .cntid_used = false, .csi_used = false},
    /* ActiveNamespaceIdList */ IdentifyCnsProps{.nsid_used = true, .cntid_used = false, .csi_used = false},
    /* NSIDList */              IdentifyCnsProps{.nsid_used = true, .cntid_used = false, .csi_used = false},
    /* NVMSetList */            IdentifyCnsProps{.nsid_used = false, .cntid_used = false, .csi_used = false},
};

// clang-format on

// from NVMe base spec rev 2.3:
// CNTID: "Controller Identifier. This field specifies the controller identifier used as part of
// some Identify operations.
//         Whether the CNTID field is used for a particular operation is indicated in FIgure 326
//         (the thing i wrote up above). If this field is NOT used as part of the Identify operation
//         the host shall set this to 0."
//
// CSI:  "Command Set Identifier. This field is CNS value specific. This field specifies the I/O
// Command
//        Set to be used by the command for CNS values that require a Command Set Identifier. For
//        Identify operations with CNS values that do not use this field, host should cler this
//        field to 0h."
//
// CNSSID: "CNS Specific Identifier. This field is dependent on the specified CNS value and if not
// defined by that CNS value,
//          then this field is reserved."

enum class IdentifyCsi : std::uint8_t {
    NVM = 0x0,  // default unused value
    KeyValue = 0x1,
    ZonedNamespace = 0x2,
    SubsystemLocalMemory = 0x3,
    ComputationalPrograms = 0x4
};

class IdentifyControllerData;
class IdentifyNamespaceData;
class IdentifyActiveNamespaceIdListData;
class IdentifyNamespaceIdentificationDescriptorListData;  // TODO: change this name ffs

struct IdentifyController {
    using Data = IdentifyControllerData;
    static constexpr auto cns = IdentifyCns::Controller;
    static constexpr std::uint16_t cntid = 0;
    static constexpr std::uint16_t cnssid = 0;
    static constexpr auto csi = IdentifyCsi::NVM;
};

struct IdentifyNamespace {  // NSID should be supplied in the command
    using Data = IdentifyNamespaceData;
    static constexpr auto cns = IdentifyCns::Namespace;
    static constexpr std::uint16_t cntid = 0;
    static constexpr std::uint16_t cnssid = 0;
    static constexpr auto csi = IdentifyCsi::NVM;
};

// returns a list of 1024 NSIDs that contains active NSIDs in increasing order that are
// greater than the value specified in the NSID field of the command
struct IdentifyActiveNamespaceIdList {
    using Data = IdentifyActiveNamespaceIdListData;
    static constexpr auto cns = IdentifyCns::ActiveNamespaceIdList;
    static constexpr std::uint16_t cntid = 0;
    static constexpr std::uint16_t cnssid = 0;
    static constexpr auto csi = IdentifyCsi::NVM;
};

struct IdentifyNamespaceIdentificationDescriptorList {  // NSID should be supplied in the command
    using Data = IdentifyNamespaceIdentificationDescriptorListData;
    static constexpr auto cns = IdentifyCns::NamespaceIdentificationDescriptorList;
    static constexpr std::uint16_t cntid = 0;
    static constexpr std::uint16_t cnssid = 0;
    static constexpr auto csi = IdentifyCsi::NVM;
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

class IdentifyNamespaceData {
public:
    static constexpr std::size_t SIZE = 4096;

    struct LbaFormat {
        std::uint16_t metadata_size;
        std::uint8_t lba_data_shift;
        std::uint8_t relative_performance;
    };

    explicit IdentifyNamespaceData(const std::byte* src);

    std::uint64_t nsze() const;
    std::uint64_t ncap() const;
    std::uint64_t nuse() const;
    std::uint8_t nlbaf() const;
    std::uint8_t flbas() const;
    std::uint8_t active_lba_format_index() const;

    LbaFormat lba_format(std::uint8_t index) const;
    LbaFormat active_lba_format() const;

    std::uint32_t lba_size_bytes() const;

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

class IdentifyActiveNamespaceIdListData {
public:
    static constexpr std::size_t SIZE = 4096;
    static constexpr std::size_t MAX_COUNT = SIZE / sizeof(std::uint32_t);

    explicit IdentifyActiveNamespaceIdListData(const std::byte* src);

    // the active NSIDs (the prefix before the first zero entry)
    std::span<const std::uint32_t> nsids() const;

    // if full then reissue the command with the last nsid to get the next nsids
    bool full() const;

private:
    std::size_t count_ = 0;
    alignas(8) std::array<std::uint32_t, MAX_COUNT> nsid_list_;
};

template<typename T, typename Expected>
concept SameAsUnqualified = std::same_as<std::remove_cvref_t<T>, Expected>;

template<typename Target>
concept IsIdentifyTarget = requires {
    typename Target::Data;
    { Target::cns } -> std::convertible_to<IdentifyCns>;
    { Target::cntid } -> SameAsUnqualified<std::uint16_t>;
    { Target::cnssid } -> SameAsUnqualified<std::uint16_t>;
    { Target::csi } -> std::convertible_to<IdentifyCsi>;
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

    static constexpr IdentifyCnsProps PROPS = identify_props[static_cast<std::size_t>(Target::cns)];

    explicit Identify(dma::DmaView result)
        requires(!PROPS.nsid_used)
        : result_{result} {
        assert(result_.size >= Data::SIZE && "identify result buffer is too small");
    }

    Identify(dma::DmaView result, std::uint32_t nsid)
        requires(PROPS.nsid_used)
        : result_{result}, nsid_{nsid} {
        assert(result_.size >= Data::SIZE && "identify result buffer is too small");
    }

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept {
        // set CDW0.PSDT = 0x0 to indicate the use of PRPs for the transfer
        wire::SubmissionEntry c = wire::SubmissionEntry::build(
            Opcode::Identify,
            this_cid,
            wire::SubmissionEntry::Psdt::Prp
        );

        c.nsid = nsid_;
        // use only prp1 as a 4 KB buffer fits exactly into a page, so no need to use prp2
        c.dptr = wire::DataPointer::prp(result_.iova);
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

    // decodes the result buffer the controller has dma'd into
    // probably is UB when called when command is not completed yet
    Data data() const {
        return Data{result_.uva};
    }

    // no need to look at Completion, as the controller DMAs the resulting data buffer that
    // describes information about the NVM subsystem, the domain,  the controller or the namespaces
    // to the DPTR. the data structure is 4096 bytes in size
    struct Completion {};

    static Completion decode_completion(const wire::CompletionEntry&) {
        return Completion{};
    }

private:
    using CNS = utils::BitField<DWRD, 0, 8>;
    using CNTID = utils::BitField<DWRD, 16, 16>;
    using CNSSID = utils::BitField<DWRD, 0, 16>;
    using CSI = utils::BitField<DWRD, 24, 8>;

    static constexpr auto cns = Target::cns;
    static constexpr auto cntid = Target::cntid;
    static constexpr auto cnssid = Target::cnssid;
    static constexpr auto csi = Target::csi;

    dma::DmaView result_;
    std::uint32_t nsid_ = 0;
};

// check whether identify targets satisfy the constraint:
static_assert(IsIdentifyTarget<IdentifyNamespace>);
static_assert(IsIdentifyTarget<IdentifyController>);
static_assert(IsIdentifyTarget<IdentifyActiveNamespaceIdList>);
static_assert(IsIdentifyTarget<IdentifyNamespaceIdentificationDescriptorList>);

using IdentifyNamespaceType = Identify<IdentifyNamespace>;
using IdentifyControllerType = Identify<IdentifyController>;
using IdentifyActiveNamespaceIdListType = Identify<IdentifyActiveNamespaceIdList>;

// check whether the identify types are command descriptors:
static_assert(CommandDescriptor<IdentifyControllerType>);
static_assert(CommandDescriptor<IdentifyNamespaceType>);
static_assert(CommandDescriptor<IdentifyActiveNamespaceIdListType>);

}  // namespace commands

}  // namespace nvme::admin
