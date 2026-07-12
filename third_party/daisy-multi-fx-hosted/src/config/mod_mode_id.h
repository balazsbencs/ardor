#pragma once
#include <cstdint>

namespace pedal {

enum class ModModeId : uint8_t {
    Chorus      = 0,
    Flanger     = 1,
    Rotary      = 2,
    Vibe        = 3,
    Phaser      = 4,
    VintTrem    = 5,
    PolyOctave  = 6,
    PatternTrem = 7,
    AutoSwell   = 8,
    FilterFx    = 9,
    FormantFx   = 10,
    Quadrature  = 11,
    Destroyer   = 12,
    COUNT       = 13,
};

} // namespace pedal
