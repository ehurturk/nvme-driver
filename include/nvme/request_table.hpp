#pragma once

#include "nvme/cid.hpp"
#include "nvme/command.hpp"
#include "nvme/queue_entry.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace nvme {

/// @brief This is a class
class RequestTable {
public:
    explicit RequestTable(Cid::value_type capacity);
    ~RequestTable() = default;

    RequestTable(const RequestTable&) = delete;
    RequestTable& operator=(const RequestTable&) = delete;
    RequestTable(RequestTable&&) = delete;
    RequestTable& operator=(RequestTable&&) = delete;

    /// @brief Allocate a new CID and mark the CID as State::InFlight
    std::optional<Cid> acquire();

    /// @brief Free the allocated CID and mark that CID slot as State::Free
    void cancel(Cid cid) noexcept;

    /// @brief Returns if the CID exists in the table
    bool pending(Cid cid) const;

    /// @brief Mark the CID slot using the given completion entry as State::Completed and mark its completion value
    std::optional<Error> complete(const wire::CompletionEntry& completion);

    /// @brief If the given CID is completed, release the CID slot and free the given CID, returning the CompletionEntry
    /// associated with the given CID
    std::optional<wire::CompletionEntry> take(Cid cid);

    /// @brief Stop waiting on the given CID. If the given CID command was completed before, calling complete() after
    /// this will result in a return of Error from complete(). Also, if the given CID was in flight, calling complete()
    /// after this call will free that CID.
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
        wire::CompletionEntry completion{};  // TODO: should this be a shared_ptr?
    };

    Slot* find(Cid cid);
    const Slot* find(Cid cid) const;

    void release(Cid cid) noexcept;

    std::vector<Slot> slots_;
    std::vector<Cid::value_type> free_;
};

}  // namespace nvme
