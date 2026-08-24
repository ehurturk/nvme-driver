#include "nvme/admin/async_event.hpp"

namespace nvme::admin::commands {

wire::SubmissionEntry AsyncEventRequest::build(std::uint16_t this_cid) const noexcept {
    return wire::SubmissionEntry::build(Opcode::AsyncEvent, this_cid);
}

AsyncEventRequest::Completion AsyncEventRequest::decode_completion(const wire::CompletionEntry& c) {
    Completion cl{};
    DWRD dw0 = c.completion_result.cdw0;
    cl.type = static_cast<AsynchronousEventType>(AET::extract(dw0));
    cl.information = static_cast<std::uint8_t>(AEI::extract(dw0));
    cl.log_page_id = static_cast<std::uint8_t>(LID::extract(dw0));
    cl.event_specific_param = c.completion_result.cdw1;

    return cl;
}

}  // namespace nvme::admin::commands
