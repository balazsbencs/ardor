#pragma once
#include <cstdint>

namespace pedal {

enum class ReverbModeId : uint8_t {
    Room        = 0,
    Hall        = 1,
    Plate       = 2,
    Spring      = 3,
    Bloom       = 4,
    Cloud       = 5,
    Shimmer     = 6,
    Chorale     = 7,
    Nonlinear   = 8,
    Swell       = 9,
    Magneto     = 10,
    Reflections = 11,
    COUNT       = 12,
};

} // namespace pedal
