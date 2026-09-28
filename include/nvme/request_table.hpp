#pragma once

#include "nvme/cid.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace nvme {

class RequestTable {
public:
    explicit RequestTable(Cid::value_type capacity);

    RequestTable(const RequestTable&) = delete;
    RequestTable& operator=(const RequestTable&) = delete;
    RequestTable(RequestTable&&) = delete;
    RequestTable& operator=(RequestTable&&) = delete;

    std::optional<Cid> acquire();

    void cancel(Cid cid) noexcept;

    bool pending(Cid cid) const;

    std::optional<Error> complete(const wire::CompletionEntry& completion);

    std::optional<wire::CompletionEntry> take(Cid cid);

    void abandon(Cid cid);

private:
    enum class State : std::uint8_t {
        Free,
        InFlight,
        Completed,
        Abandoned,
    };

    struct Slot {
        State state = State::Free;
        wire::CompletionEntry completion{};
    };

    Slot* find(Cid cid);
    const Slot* find(Cid cid) const;

    void release(Cid cid) noexcept;

    std::vector<Slot> slots_;
    std::vector<Cid::value_type> free_;
};

}  // namespace nvme
