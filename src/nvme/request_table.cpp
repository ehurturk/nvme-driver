#include "nvme/request_table.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

namespace nvme {

RequestTable::RequestTable(Cid::value_type capacity) {
    if (capacity == 0 || capacity == 0xFFFF) {
        throw std::invalid_argument("Invalid CID capacity");
    }

    slots_.resize(capacity);
    free_.reserve(capacity);

    for (std::uint32_t cid = capacity; cid > 0; cid--) {
        free_.push_back(static_cast<Cid::value_type>(cid - 1));
    }
}

std::optional<Cid> RequestTable::acquire() {
    if (free_.empty()) {
        return std::nullopt;
    }

    const Cid cid{free_.back()};
    free_.pop_back();
    slots_[cid.value].state = State::InFlight;
    return cid;
}

void RequestTable::cancel(Cid cid) noexcept {
    release(cid);
}

bool RequestTable::pending(Cid cid) const {
    const Slot* slot = find(cid);
    return slot != nullptr && slot->state != State::Free;
}

std::optional<Error> RequestTable::complete(const wire::CompletionEntry& completion) {
    Slot* slot = find(Cid{completion.cid});
    if (slot == nullptr || slot->state == State::Free) {
        return Error{
            .reason = std::format("Completion for CID {}, which was never submitted", completion.cid),
            .severity = Error::Severity::ERROR
        };
    }

    switch (slot->state) {
        case State::Abandoned:
            // nobody is waiting on this result any more, might as well drop it and release the CID
            release(Cid{completion.cid});
            return std::nullopt;
        case State::Completed:
            return Error{
                .reason = std::format("Duplicate completion for CID {}, which was already completed", completion.cid),
                .severity = Error::Severity::ERROR
            };
        case State::InFlight:
        case State::Free:
            break;
    }

    slot->completion = completion;
    slot->state = State::Completed;
    return std::nullopt;
}

std::optional<wire::CompletionEntry> RequestTable::take(Cid cid) {
    const Slot* slot = find(cid);
    if (slot == nullptr || slot->state != State::Completed) {
        return std::nullopt;
    }

    const wire::CompletionEntry completion = slot->completion;
    release(cid);
    return completion;
}

void RequestTable::abandon(Cid cid, std::optional<dma::DmaBuffer> buffer) {
    Slot* slot = find(cid);
    if (slot == nullptr) {
        return;
    }

    if (slot->state == State::Completed) {
        release(cid);
    } else if (slot->state == State::InFlight) {
        slot->state = State::Abandoned;
        slot->buffer = std::move(buffer);
    }
}

RequestTable::Slot* RequestTable::find(Cid cid) {
    return cid.value < slots_.size() ? &slots_[cid.value] : nullptr;
}

const RequestTable::Slot* RequestTable::find(Cid cid) const {
    return cid.value < slots_.size() ? &slots_[cid.value] : nullptr;
}

void RequestTable::release(Cid cid) noexcept {
    slots_[cid.value] = Slot{};
    free_.push_back(cid.value);
}

}  // namespace nvme
