#pragma once
#include "mod_param_id.h"

namespace pedal {
namespace mod_fx {

struct ParamSet {
    float speed;   // LFO rate in Hz (or mode-specific)
    float depth;   // modulation depth 0..1
    float mix;     // wet/dry 0..1
    float tone;    // filter position 0..1 (0.5=flat)
    float p1;      // mode-specific parameter 1
    float p2;      // mode-specific parameter 2
    float level;   // output gain 0..2 (unity at 1.0)

    float get(ParamId id) const;
    static ParamSet make_default();
};

} // namespace mod_fx
} // namespace pedal
