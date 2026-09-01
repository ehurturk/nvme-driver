#pragma once

#include <cstdint>

namespace nvme::io {

enum class Opcode : std::uint8_t {
    Flush = 0x00,
    Write = 0x01,
    Read = 0x02,
    Copy = 0x19,
};

} // namespace nvme::io