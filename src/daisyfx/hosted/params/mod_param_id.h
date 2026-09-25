#pragma once
#include <cstdint>

namespace pedal {
namespace mod_fx {

enum class ParamId : uint8_t {
    Speed = 0,
    Depth = 1,
    Mix   = 2,
    Tone  = 3,
    P1    = 4,
    P2    = 5,
    Level = 6,
    // Optional mode-specific controls. They follow the original seven so the
    // host's descriptor indexes, which scenes store, never move.
    P3    = 7,
    P4    = 8,
    COUNT = 9,
};

} // namespace mod_fx
} // namespace pedal
