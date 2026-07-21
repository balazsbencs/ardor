#pragma once
#include "reverb_param_id.h"

namespace pedal {
namespace reverb_fx {

struct ParamSet {
    float decay;     // reverb time in seconds (range varies per mode)
    float pre_delay; // pre-delay 0..0.5s; Magneto/Nonlinear: feedback 0..0.95
    float mix;       // wet/dry 0..1
    float tone;      // filter 0..1 (0.5=flat, <0.5=LP, >0.5=HP)
    float mod;       // modulation amount 0..1
    float param1;    // per-algorithm meaning
    float param2;    // per-algorithm meaning

    float get(ParamId id) const;
    static ParamSet make_default();
};

} // namespace reverb_fx
} // namespace pedal
