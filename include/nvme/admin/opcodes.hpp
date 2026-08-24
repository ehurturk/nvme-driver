#pragma once

#include <cstdint>

namespace nvme::admin {

enum class Opcode : std::uint8_t {
    DeleteIoSq = 0x00,
    CreateIoSq = 0x01,
    GetLogPage = 0x02,
    DeleteIoCq = 0x04,
    CreateIoCq = 0x05,
    Identify = 0x06,
    Abort = 0x08,
    SetFeatures = 0x09,
    GetFeatures = 0x0A,
    AsyncEvent = 0x0C,
};

}  // namespace nvme::admin
