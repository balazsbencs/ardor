#pragma once
#include <cstdint>

namespace pedal {

enum class DelayModeId : uint8_t {
    Digital = 0,
    Tape    = 1,
    Dual    = 2,
    Filter  = 3,
    Lofi    = 4,
    DBucket = 5,
    Duck    = 6,
    Pattern = 7,
    Swell   = 8,
    Trem    = 9,
    COUNT   = 10,
};

} // namespace pedal
