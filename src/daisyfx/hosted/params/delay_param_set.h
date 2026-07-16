#pragma once
#include "delay_param_id.h"

namespace pedal {
namespace delay_fx {

struct ParamSet {
    float time;     // delay time in seconds (0.06..2.5; Lo-Fi: 0.002..2.5)
    float repeats;  // feedback 0..0.98
    float mix;      // wet/dry 0..1
    float filter;   // filter position 0..1 (0.5=flat, <0.5=LP, >0.5=HP)
    float grit;     // dirt/saturation amount 0..1
    float mod_spd;  // modulation rate in Hz 0.05..10
    float mod_dep;  // modulation depth 0..1

    float get(ParamId id) const;
    static ParamSet make_default();
};

} // namespace delay_fx
} // namespace pedal
