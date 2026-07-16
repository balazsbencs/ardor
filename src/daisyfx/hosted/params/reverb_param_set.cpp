#include "reverb_param_set.h"

namespace pedal {
namespace reverb_fx {

float ParamSet::get(ParamId id) const {
    switch (id) {
        case ParamId::Decay:    return decay;
        case ParamId::PreDelay: return pre_delay;
        case ParamId::Mix:      return mix;
        case ParamId::Tone:     return tone;
        case ParamId::Mod:      return mod;
        case ParamId::Param1:   return param1;
        case ParamId::Param2:   return param2;
        default:                return 0.0f;
    }
}

ParamSet ParamSet::make_default() {
    return ParamSet{
        .decay     = 2.0f,
        .pre_delay = 0.02f,
        .mix       = 0.5f,
        .tone      = 0.5f,
        .mod       = 0.0f,
        .param1    = 0.0f,
        .param2    = 0.5f,
    };
}

} // namespace reverb_fx
} // namespace pedal
