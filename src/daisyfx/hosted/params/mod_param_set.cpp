#include "mod_param_set.h"

namespace pedal {
namespace mod_fx {

float ParamSet::get(ParamId id) const {
    switch (id) {
        case ParamId::Speed: return speed;
        case ParamId::Depth: return depth;
        case ParamId::Mix:   return mix;
        case ParamId::Tone:  return tone;
        case ParamId::P1:    return p1;
        case ParamId::P2:    return p2;
        case ParamId::Level: return level;
        default:             return 0.0f;
    }
}

ParamSet ParamSet::make_default() {
    return ParamSet{
        .speed = 0.5f,
        .depth = 0.5f,
        .mix   = 0.5f,
        .tone  = 0.5f,
        .p1    = 0.0f,
        .p2    = 0.0f,
        .level = 1.0f,
    };
}

} // namespace mod_fx
} // namespace pedal
