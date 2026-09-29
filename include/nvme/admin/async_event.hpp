#pragma once

#include "nvme/admin/opcodes.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"
#include "utils.hpp"

#include <cstdint>
#include <string_view>

namespace nvme::admin::commands {

struct AsyncEventRequest {
    static constexpr std::string_view name = "Asynchronous Event Request";

    wire::SubmissionEntry build(std::uint16_t this_cid) const noexcept;

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

    static Completion decode_completion(const wire::CompletionEntry& c);

private:
    using AET = utils::BitField<std::uint32_t, 0, 3>;
    using AEI = utils::BitField<std::uint32_t, 8, 8>;
    using LID = utils::BitField<std::uint32_t, 16, 8>;
};

static_assert(CommandDescriptor<AsyncEventRequest>);

}  // namespace nvme::admin::commands
