#pragma once

#include <stdint.h>

#include <cstddef>
#include <type_traits>

#include "utils.hpp"

/*
*
* Controller registers:
  CAP:         0h - 7h
  VS:          8h - Bh
  CC           14h - 17h
  CSTS         1Ch - 1Fh
  AQA          24h - 27h
  ASQ          28h - 2Fh
  ACQ          30h - 3Bh

  clang-format off
  ASQ0 Tail Doorbell  1000h                          - 1003h
  ACQ0 Head Doorbell  1000h + (1 * (4 << CAP.DSTRD)) - 1003h + (1 * 4 << CAP.DSTRD)

  SQ1 Tail Doorbell   1000h + (2 * (4 << CAP.DSTRD)) - 1003h + (2 * 4 << CAP.DSTRD)
  CQ1 Tail Doorbell   1000h + (3 * (4 << CAP.DSTRD)) - 1003h + (3 * 4 << CAP.DSTRD)

  SQ2 Tail Doorbell   1000h + (4 * (4 << CAP.DSTRD)) - 1003h + (4 * 4 << CAP.DSTRD)
  CQ2 Tail Doorbell   1000h + (5 * (4 << CAP.DSTRD)) - 1003h + (5 * 4 << CAP.DSTRD)

  SQyTDBL             1000h + (2y * (4 << CAP.DSTRD)) - 1003h + (2y * (4 << CAP.DSTRD))
  CQyHDBL             1000h + ((2y+1) * (4 << CAP.DSTRD)) - 1003h + ((2y+1) * (4 << CAP.DSTRD))
  clang-format on

  Controller Capabilities:
   - Doorbell Stride (DSTRD): CAP[35:32]

Queue structures:
  Submission Queue Entry
  Completion Queue Entry
  phase tag
  queue head/tail

Admin commands:
  Identify Controller
  Identify Namespace
  Create I/O Completion Queue
  Create I/O Submission Queue

NVM commands:
  Read
  Write
  Flush, optional


NVMe devices allow host software (the NVMe driver) to allocate queue pairs in
host memory.
- Host: system that the NVMe SSD is plugged into.

A queue pair consists of 2 queues:
 - submission queue
 - completion queue

These queues are circular ring buffers of fixed size entries.
 - Submission queue: array of 64-byte command structures + 2 integers (head +
tail indices)
 - Completion queue: array of 16-byte completion structures + 2 integers (head +
tail indices)

Doorbell registers are involved to:
 1) Notify the NVMe controller that a submission is placed into the SQ
 2) Notify the host that an I/O completion has done and placed into the CQ

An I/O is submitted to an NVMe device by constructing a 64 byte command, placing
it into the submission queue at the current location of the submission queue
tail index, and then writing the new index of the submission queue tail to the
submission queue doorbell register.
 - The I/O commands can be batched by placing the whole set of commands into
open slots in the ring and then writing the doorbell just one time.

The command describes the IO operation and also, if necessary, a location in
host memory containing a descriptor for host memory associated with the command.
This host memory is the data to be written on a write command, or the location
to place the data on a read command.
 - Data is trasferred to / from this host memory location using a *DMA engine*
on the NVMe device.

The completion queue has a similar structure, however, in this case the NVMe
controller is the device that is writing entries to the ring. Each entry has a
phase bit that toggles between 0 and 1 on each loop through the entire ring.
When a queue pair is set up to generate interrupts, the interupt contains the
index of the completion queue head.
 - Interrupts can be disabled (or ignored), and instead, the phase bit can be
polled. Note that even though polling is a form of busy waiting, in this case it
is far more efficient than interrupts which incur context switch costs. SPDK
also polls on the phase bit.

*/

namespace nvme
{

enum class ControllerType : uint8_t
{
    NVME_CTL_IO    = 1, // io controller (standard NVMe controller)
    NVME_CTL_DISC  = 2, // discovery controller (NVMe-oF specific)
    NVME_CTL_ADMIN = 3  // admin controller (opt)
};

enum class TransportType : uint8_t
{
    NVME_TRTYPE_PCI  = 0, // PCI/PCIe
    NVME_TRTYPE_RDMA = 1, // RDMA
    NVME_TRTYPE_FC   = 2, // Fibre Channel
    NVME_TRTYPE_TCP  = 3  // TCP
};

namespace constants
{
// Some of the constants here are taken from linux:
// include/linux/nvme.h

static constexpr size_t AQ_DEPTH = 32;
static_assert(AQ_DEPTH <= 4096 && AQ_DEPTH > 0);

static constexpr size_t IO_DEPTH = 1024;

// For the queue entry size constants,
// the NVMe spec mandates that "the value
// is in bytes and is specified as a power of
// two (2^n)."
static constexpr size_t ADM_SQES = 6;
static constexpr size_t ADM_CQES = 4;

static constexpr size_t IO_SQES = 6;
static constexpr size_t IO_CQES = 4;
} // namespace constants

namespace bar0
{
constexpr std::size_t CAP    = 0x00;
constexpr std::size_t VS     = 0x08;
constexpr std::size_t INTMS  = 0x0c;
constexpr std::size_t INTMC  = 0x10;
constexpr std::size_t CC     = 0x14;
constexpr std::size_t CSTS   = 0x1c;
constexpr std::size_t NSSR   = 0x20;
constexpr std::size_t AQA    = 0x24;
constexpr std::size_t ASQ    = 0x28;
constexpr std::size_t ACQ    = 0x30;
constexpr std::size_t CMBLOC = 0x38;
constexpr std::size_t CMBSZ  = 0x3c;
} // namespace bar0

class NVMeBar0
{
  public:
    explicit NVMeBar0(void* base) : bar0_{base}
    {}

    std::uint64_t cap() const
    {
        return bar0_.read64(bar0::CAP);
    }
    std::uint64_t vs() const
    {
        return bar0_.read64(bar0::VS);
    }
    std::uint64_t intms() const
    {
        return bar0_.read64(bar0::INTMS);
    }
    std::uint64_t intmc() const
    {
        return bar0_.read64(bar0::INTMC);
    }
    std::uint64_t cc() const
    {
        return bar0_.read64(bar0::CC);
    }
    std::uint64_t csts() const
    {
        return bar0_.read64(bar0::CSTS);
    }
    std::uint64_t nssr() const
    {
        return bar0_.read64(bar0::NSSR);
    }
    std::uint64_t aqa() const
    {
        return bar0_.read64(bar0::AQA);
    }
    std::uint64_t asq() const
    {
        return bar0_.read64(bar0::ASQ);
    }
    std::uint64_t acq() const
    {
        return bar0_.read64(bar0::ACQ);
    }
    std::uint64_t cmbloc() const
    {
        return bar0_.read64(bar0::CMBLOC);
    }
    std::uint64_t cmbsz() const
    {
        return bar0_.read64(bar0::CMBSZ);
    }

    void intms(std::uint32_t value)
    {
        bar0_.write32(bar0::CAP, value);
    }
    void intmc(std::uint32_t value)
    {
        bar0_.write32(bar0::INTMC, value);
    }
    void cc(std::uint32_t value)
    {
        bar0_.write32(bar0::CC, value);
    }
    void nssr(std::uint32_t value)
    {
        bar0_.write32(bar0::NSSR, value);
    }
    void aqa(std::uint32_t value)
    {
        bar0_.write32(bar0::AQA, value);
    }
    void asq(std::uint64_t value)
    {
        bar0_.write64(bar0::ASQ, value);
    }
    void acq(std::uint64_t value)
    {
        bar0_.write64(bar0::ACQ, value);
    }

  private:
    utils::MmioRegion bar0_;
};

// Controller Capabilities
namespace cap
{
using MQES = utils::BitField<uint64_t, 0, 16>; // Max Queue Entries
// 1: Vendor specific
// 0: Weighted Round Robinw with Urgent Priority Class
using AMS     = utils::BitField<uint64_t, 17, 2>; // Arbitration Mechanism Supported
using TIMEOUT = utils::BitField<uint64_t, 24, 8>; // Timeout (500ms units)
using DSTR    = utils::BitField<uint64_t, 32, 4>; // Doorbell Stride
using NSSRS   = utils::BitField<uint64_t, 36, 1>; // NVM Subsystem Reset
using CSS     = utils::BitField<uint64_t, 37, 8>; // Command Sets Supported
using MPSMin  = utils::BitField<uint64_t, 48, 4>; // Min Page Size (2^(12+n))
using MPSMax  = utils::BitField<uint64_t, 52, 4>; // Max Page Size
using PMRS    = utils::BitField<uint64_t, 56, 1>; // Persistent Memory Region Supported
using CMBS    = utils::BitField<uint64_t, 57, 1>; // CMB Supported

namespace css
{
// Mask the Command Sets Supported field with these
// bitmasks
//     e.g. capCss & NCSS_BIT | capCss & IOCSS_BIT
constexpr uint8_t NCSS_BIT    = 0x1;
constexpr uint8_t IOCSS_BIT   = 0x1 << 6;
constexpr uint8_t NOIOCSS_BIT = 0x1 << 7;
} // namespace css

} // namespace cap

// Version
namespace vs
{
using TertiaryVersion = utils::BitField<uint32_t, 0, 8>;
using MinorVersion    = utils::BitField<uint32_t, 8, 8>;
using MajorVersion    = utils::BitField<uint32_t, 16, 16>;
} // namespace vs

// Controller Configuration
namespace cc
{
using EN     = utils::BitField<uint32_t, 0, 1>;  // Enable
using CSS    = utils::BitField<uint32_t, 4, 3>;  // IO Command Set Selected
using MPS    = utils::BitField<uint32_t, 7, 4>;  // Memory Page Size
using AMS    = utils::BitField<uint32_t, 11, 3>; // Arbitration Mechanism Selected
using SHN    = utils::BitField<uint32_t, 14, 2>; // Shutdown Notification
using IOSQES = utils::BitField<uint32_t, 16, 4>; // IO Submission Queue Entry Size
using IOCQES = utils::BitField<uint32_t, 20, 4>; // IO Completion Queue Entry Size
namespace css
{
constexpr uint8_t NCSS    = 0b000;
constexpr uint8_t IOCSS   = 0b110;
constexpr uint8_t NOIOCSS = 0b111;
} // namespace css
} // namespace cc

namespace csts
{
using RDY = utils::BitField<uint32_t, 0, 1>; // Ready
}

namespace aqa
{
using ASQS = utils::BitField<uint32_t, 0, 12>;
using ACQS = utils::BitField<uint32_t, 16, 12>;
} // namespace aqa

namespace asq
{
using ASQB = utils::BitField<uint64_t, 12, 52>;
}

namespace acq
{
using ACQB = utils::BitField<uint64_t, 12, 52>;
}

namespace command
{

enum class Opcode : uint8_t
{
    Flush            = 0x00,
    Write            = 0x01,
    Read             = 0x02,
    WriteUncorrected = 0x04,
    Compare          = 0x05,
    WriteZeroes      = 0x08,
    Dsm              = 0X09,
    Verify           = 0X0C,
    ResvRegister     = 0x0D,
    ResvReport       = 0X0E,
};

enum class AdminOpcode : uint8_t
{
    DeleteSq                 = 0x00,
    CreateSq                 = 0x01,
    GetLogPage               = 0x02,
    DeleteCq                 = 0x04,
    CreateCq                 = 0x05,
    Identify                 = 0x06,
    AbortCmd                 = 0x08,
    SetFeatures              = 0x09,
    GetFeatures              = 0x0a,
    AsyncEvent               = 0x0c,
    NsManagement             = 0x0d,
    DeviceSelfTest           = 0x14,
    NsAttach                 = 0x15,
    KeepAlive                = 0x18,
    DirectiveSend            = 0x19,
    DirectiveReceive         = 0x1a,
    VirtualizationManagement = 0x1c,
    NvmeMiSend               = 0x1d,
    NvmeMiRecv               = 0x1e,
    DbBufConfig              = 0x7C,
};

} // namespace command

}; // namespace nvme
