#pragma once

#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cstdint>

namespace nvme::admin::commands {

struct AsyncEventRequest {
    CommonCommand build(std::uint16_t this_cid) const noexcept {
        return CommonCommand::build(Opcode::AsyncEvent, this_cid);
    }

    enum class AsynchronousEventType : std::uint8_t {
        ErrorStatus = 0b000,
        SMART_HealthStatus = 0b001,
        Notice = 0b010,
        Immediate = 0b011,
        OneShot = 0b100,
        IOCommandSpecificStatus = 0b110
    };

    struct Completion {
        // cdw0
        AsynchronousEventType type;
        std::uint8_t information;
        std::uint8_t log_page_id;

        // cdw1
        DWRD event_specific_param;
    };

    static Completion decode_completion(const CompletionQueueEntry& c) {
        Completion cl{};
        DWRD dw0 = c.completion_result.cdw0;
        cl.type = static_cast<AsynchronousEventType>(AET::extract(dw0));
        cl.information = static_cast<std::uint8_t>(AEI::extract(dw0));
        cl.log_page_id = static_cast<std::uint8_t>(LID::extract(dw0));
        cl.event_specific_param = c.completion_result.cdw1;

        return cl;
    }

private:
    using AET = utils::BitField<std::uint32_t, 0, 3>;
    using AEI = utils::BitField<std::uint32_t, 8, 8>;
    using LID = utils::BitField<std::uint32_t, 16, 8>;
};

static_assert(CommandDescriptor<AsyncEventRequest>);

}  // namespace nvme::admin::commands
