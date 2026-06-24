#pragma once

#include "traits.hpp"

namespace nvme
{
    template <class Kind, std::size_t N = QueueTraits<Kind>::depth>
    class CompletionQueue
    {
        public:
            using Trait = QueueTraits<Kind>;
            using Entry = Trait::CompletionEntry;

            static constexpr std::size_t depth = N;

        private:
            std::size_t head_{};
    };
}
