#pragma once
#include <cstdint>

namespace pedal {
namespace delay_fx {

enum class ParamId : uint8_t {
    Time    = 0,
    Repeats = 1,
    Mix     = 2,
    Filter  = 3,
    Grit    = 4,
    ModSpd  = 5,
    ModDep  = 6,
    // Stereo width of the repeats, after the original seven so the host's
    // descriptor indexes, which scenes store, never move. 1 = full, 0 = mono.
    Width   = 7,
    COUNT   = 8,
};

} // namespace delay_fx
} // namespace pedal
