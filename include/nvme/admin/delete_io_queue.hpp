#pragma once

#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cassert>
#include <cstdint>

namespace nvme::admin::commands {

// The delete IO CQ command is used to delete an IO completion queue.
// It uses the Command DWORD 10 field.
// NOTE: - After this command has been completed, the PRP List that describes
//         the CQ may be deallocated by the host.
//       - Host shall ensure that any associated I/O Submission Queue is deleted
//         deleted prior to deleting a CQ.
struct DeleteIoCq {
    std::uint16_t qid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept;

    struct Completion {};

    static Completion decode_completion(const CompletionQueueEntry&);

private:
    // Identifier of the queue (SQ/DQ) to be deleted
    using QID = utils::BitField<DWRD, 0, 16>;
};

static_assert(CommandDescriptor<DeleteIoCq>);

// The delete IO SQ command is used to delete an IO submission queue.
// It uses the Command DWORD 10 field.
// NOTE: - After this command has been completed, the PRP List that describes
//         the SQ may be deallocated by the host.
struct DeleteIoSq {
    std::uint16_t qid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept;

    struct Completion {};

    static Completion decode_completion(const CompletionQueueEntry&);

private:
    // Identifier of the queue (SQ/DQ) to be deleted
    using QID = utils::BitField<DWRD, 0, 16>;
};

static_assert(CommandDescriptor<DeleteIoSq>);

}  // namespace nvme::admin::commands
