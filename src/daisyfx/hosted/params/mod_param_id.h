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
    COUNT = 7,
};

} // namespace mod_fx
} // namespace pedal
