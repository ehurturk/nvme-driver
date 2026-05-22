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

  ASQ0 Tail Doorbell  1000h                          - 1003h
  ACQ0 Head Doorbell  1000h + (1 * (4 << CAP.DSTRD)) - 1003h + (1 * 4 <<
CAP.DSTRD) SQ1 Tail Doorbell   1000h + (2 * (4 << CAP.DSTRD)) - 1003h + (2 * 4
<< CAP.DSTRD) CQ1 Tail Doorbell   1000h + (3 * (4 << CAP.DSTRD)) - 1003h + (3 *
4 << CAP.DSTRD) SQ2 Tail Doorbell   1000h + (4 * (4 << CAP.DSTRD)) - 1003h + (4
* 4 << CAP.DSTRD) CQ2 Tail Doorbell   1000h + (5 * (4 << CAP.DSTRD)) - 1003h +
(5 * 4 << CAP.DSTRD)

  SQyTDBL             1000h + (2y * (4 << CAP.DSTRD)) - 1003h + (2y * (4 <<
CAP.DSTRD)) CQyHDBL             1000h + ((2y+1) * (4 << CAP.DSTRD)) - 1003h +
((2y+1) * (4 << CAP.DSTRD))

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

namespace nvme {

enum class ControllerType : uint8_t {
  NVME_CTL_IO = 1,    // io controller (standard NVMe controller)
  NVME_CTL_DISC = 2,  // discovery controller (NVMe-oF specific)
  NVME_CTL_ADMIN = 3  // admin controller (opt)
};

enum class TransportType : uint8_t {
  NVME_TRTYPE_PCI = 0,   // PCI/PCIe
  NVME_TRTYPE_RDMA = 1,  // RDMA
  NVME_TRTYPE_FC = 2,    // Fibre Channel
  NVME_TRTYPE_TCP = 3    // TCP
};

// Linux uses 32 as admin queue depth, so i went with it
constexpr static int AQ_DEPTH = 32;

struct alignas(4096) NvmeBar0 {
  volatile uint64_t cap;     // 0x0000 Controller Capabilities
  volatile uint32_t vs;      // 0x0008 Version
  volatile uint32_t intms;   // 0x000C Interrupt Mask Set
  volatile uint32_t intmc;   // 0x0010 Interrupt Mask Clear
  volatile uint32_t cc;      // 0x0014 Controller Configuration
  uint32_t _pad0;            // 0x0018 padding
  volatile uint32_t csts;    // 0x001C Controller Status
  volatile uint32_t nssr;    // 0x0020 NVM Subsystem Reset
  volatile uint32_t aqa;     // 0x0024 Admin Queue Attributes
  volatile uint64_t asq;     // 0x0028 Admin SQ Base Address
  volatile uint64_t acq;     // 0x0030 Admin CQ Base Address
  volatile uint32_t cmbloc;  // 0x0038 CMB Location
  volatile uint32_t cmbsz;   // 0x003C CMB Size
                             // etc...
};

// address should be at bar0_base + 0x1000 + (2 * qid) * (4 << dstrd)
struct NvmeDoorbells {
  // doorbell registers here (they are part of BAR0)
};

static_assert(std::is_standard_layout_v<NvmeBar0>);
static_assert(offsetof(NvmeBar0, vs) == 0x0008);
static_assert(offsetof(NvmeBar0, intms) == 0x000C);
static_assert(offsetof(NvmeBar0, cc) == 0x0014);
static_assert(offsetof(NvmeBar0, csts) == 0x001C);
static_assert(offsetof(NvmeBar0, asq) == 0x0028);
static_assert(offsetof(NvmeBar0, acq) == 0x0030);
static_assert(offsetof(NvmeBar0, cmbloc) == 0x0038);

struct QueuePair {
  // submission queue
  // completion queue
  // submission queue tail index
  // completion queue head index
  // tracker array for the queues
  // memory pool of request structs
  // free list of tracker CIDs
  // expected phase
};

struct Tracker {
  // CID
  // Request*
  // PRP list memory?
};

struct Request {
  // command
  // callback fn
  // callback args
  // buffer info
};

namespace cap {

using Mqes = utils::BitField<uint64_t, 0, 16>;     // Max Queue Entries
using Timeout = utils::BitField<uint64_t, 24, 8>;  // Timeout (500ms units)
using DStride = utils::BitField<uint64_t, 32, 4>;  // Doorbell Stride
using Nssrs = utils::BitField<uint64_t, 36, 1>;    // NVM Subsystem Reset
using Css = utils::BitField<uint64_t, 37, 8>;      // Command Sets Supported
using Mpsmin = utils::BitField<uint64_t, 48, 4>;   // Min Page Size (2^(12+n))
using Mpsmax = utils::BitField<uint64_t, 52, 4>;   // Max Page Size
using Pmrs =
    utils::BitField<uint64_t, 56, 1>;  // Persistent Memory Region Supported
using Cmbs = utils::BitField<uint64_t, 57, 1>;  // CMB Supported

}  // namespace cap

namespace cc {

using En = utils::BitField<uint32_t, 0, 1>;    // Enable
using Css = utils::BitField<uint32_t, 4, 3>;   // IO Command Set Selected
using Mps = utils::BitField<uint32_t, 7, 4>;   // Memory Page Size
using Ams = utils::BitField<uint32_t, 11, 3>;  // Arbitration Mechanism Selected
using Shn = utils::BitField<uint32_t, 14, 2>;  // Shutdown Notification
using Iosqes =
    utils::BitField<uint32_t, 16, 4>;  // IO Submission Queue Entry Size
using Iocqes =
    utils::BitField<uint32_t, 20, 4>;  // IO Completion Queue Entry Size

}  // namespace cc

namespace command {

enum class Opcode : uint8_t {
  Flush = 0x00,
  Write = 0x01,
  Read = 0x02,
  WriteUncorrected = 0x04,
  Compare = 0x05,
  WriteZeroes = 0x08,
  Dsm = 0X09,
  Verify = 0X0C,
  ResvRegister = 0x0D,
  ResvReport = 0X0E,
};

enum class AdminOpcode : uint8_t {
  DeleteSq = 0x00,
  CreateSq = 0x01,
  GetLogPage = 0x02,
  DeleteCq = 0x04,
  CreateCq = 0x05,
  Identify = 0x06,
  AbortCmd = 0x08,
  SetFeatures = 0x09,
  GetFeatures = 0x0a,
  AsyncEvent = 0x0c,
  NsManagement = 0x0d,
  DeviceSelfTest = 0x14,
  NsAttach = 0x15,
  KeepAlive = 0x18,
  DirectiveSend = 0x19,
  DirectiveReceive = 0x1a,
  VirtualizationManagement = 0x1c,
  NvmeMiSend = 0x1d,
  NvmeMiRecv = 0x1e,
  DbBufConfig = 0x7C,
};

}  // namespace command

};  // namespace nvme
