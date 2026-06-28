#pragma once

#include <assert.h>

#include "../utils.hpp"

namespace nvme
{
namespace layout
{

namespace abort_cdw10
{
using SQID = utils::BitField<DWRD, 0, 16>;
using CID  = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(std::uint16_t sqid, std::uint16_t cid) noexcept
{
    return utils::DwordBuilder<>{}.set<SQID>(sqid).set<CID>(cid).get();
}
} // namespace abort_cdw10

namespace create_io_q_cdw10
{
using QID   = utils::BitField<DWRD, 0, 16>;
using QSIZE = utils::BitField<DWRD, 16, 16>;

// NOTE: Qsize should be 1s based
inline DWRD encode(std::uint16_t qid, std::uint16_t qsize) noexcept
{
    assert(qsize > 0);

    return utils::DwordBuilder<>{}.set<QID>(qid).set<QSIZE>(qsize - 1).get();
}
} // namespace create_io_q_cdw10

namespace create_io_cq_cdw11
{
using PC  = utils::BitField<DWRD, 0, 1>;
using IEN = utils::BitField<DWRD, 1, 1>;
using IV  = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(bool physically_contiguous, bool interrupts_enabled, std::uint16_t interrupt_vector) noexcept
{
    return utils::DwordBuilder<>{}
            .set<PC>(physically_contiguous)
            .set<IEN>(interrupts_enabled)
            .set<IV>(interrupt_vector)
            .get();
}
} // namespace create_io_cq_cdw11

enum class QueuePriority : std::uint8_t
{
    Urgent = 0b00,
    High   = 0b01,
    Medium = 0b10,
    Low    = 0b11,
};

namespace create_io_sq_cdw11
{
using PC    = utils::BitField<DWRD, 0, 1>;
using QPRIO = utils::BitField<DWRD, 1, 2>;
using CQID  = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(bool physically_contiguous, QueuePriority priority, std::uint16_t cqid) noexcept
{
    return utils::DwordBuilder<>{}
            .set<PC>(physically_contiguous)
            .set<QPRIO>(utils::to_underlying(priority))
            .set<CQID>(cqid)
            .get();
}
} // namespace create_io_sq_cdw11

namespace create_io_sq_cdw12
{
using NVMSETID = utils::BitField<DWRD, 0, 16>;

inline DWRD encode(std::uint16_t nvmsetid) noexcept
{
    return utils::DwordBuilder<>{}.set<NVMSETID>(nvmsetid).get();
}
} // namespace create_io_sq_cdw12

namespace identify_cdw10
{
using CNS   = utils::BitField<DWRD, 0, 8>;
using CNTID = utils::BitField<DWRD, 16, 16>;

inline DWRD encode(std::uint8_t cns, std::uint16_t cntid) noexcept
{
    return utils::DwordBuilder<>{}.set<CNS>(cns).set<CNTID>(cntid).get();
}
} // namespace identify_cdw10

namespace identify_cdw11
{
using CNSSID = utils::BitField<DWRD, 0, 16>;
using CSI    = utils::BitField<DWRD, 24, 8>;

inline DWRD encode(std::uint16_t cnssid, std::uint8_t csi) noexcept
{
    return utils::DwordBuilder<>{}.set<CNSSID>(cnssid).set<CSI>(csi).get();
}
} // namespace identify_cdw11

namespace delete_io_q_cdw10
{
// Identifier of the queue (SQ/DQ) to be deleted.
using QID = utils::BitField<DWRD, 0, 16>;

inline DWRD encode(std::uint16_t qid) noexcept
{
    assert(qid != 0 && "Admin CQ can't be deleted.");
    return utils::DwordBuilder<>{}.set<QID>(qid).get();
}
} // namespace delete_io_q_cdw10

} // namespace layout

} // namespace nvme
