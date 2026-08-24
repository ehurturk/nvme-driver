#pragma once

#include "nvme/queue_entry.hpp"

#include <concepts>
#include <cstdint>

namespace nvme {

template<typename C>
concept CommandDescriptor = std::default_initializable<C> &&
                            requires(const C c, std::uint16_t cid, const CompletionQueueEntry& e) {
                                { c.build(cid) } noexcept -> std::same_as<CommonCommand>;
                                typename C::Completion;
                                { C::decode_completion(e) } -> std::same_as<typename C::Completion>;
                            };

}  // namespace nvme
