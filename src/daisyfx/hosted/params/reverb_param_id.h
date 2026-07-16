#pragma once
#include <cstdint>

namespace pedal {
namespace reverb_fx {

enum class ParamId : uint8_t {
    Decay    = 0,
    PreDelay = 1,
    Mix      = 2,
    Tone     = 3,
    Mod      = 4,
    Param1   = 5,
    Param2   = 6,
    COUNT    = 7,
};

} // namespace reverb_fx
} // namespace pedal
