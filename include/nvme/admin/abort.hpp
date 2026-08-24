#pragma once

#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cstdint>

namespace nvme::admin::commands {

// abort (0x08)
// completion dw0 bit 0: 1 = not aborted, 0 = abort was not applied
struct Abort {
    std::uint16_t sqid{};
    std::uint16_t cid{};

    CommonCommand build(std::uint16_t this_cid) const noexcept {
        CommonCommand c = CommonCommand::build(Opcode::Abort, this_cid);
        c.cdw10 = utils::DwordBuilder<>{}.set<SQID>(sqid).set<CID>(cid).get();
        return c;
    }

    struct Completion {
        bool immediate_abort_not_performed;
    };

    static Completion decode_completion(const CompletionQueueEntry& c) {
        return Completion{.immediate_abort_not_performed = (c.completion_result.cdw0 & 0x1) == 1};
    }

private:
    using SQID = utils::BitField<DWRD, 0, 16>;
    using CID = utils::BitField<DWRD, 16, 16>;
};

static_assert(CommandDescriptor<Abort>);

}  // namespace nvme::admin::commands
