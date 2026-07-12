#include "delay_param_set.h"

namespace pedal {
namespace delay_fx {

float ParamSet::get(ParamId id) const {
    switch (id) {
        case ParamId::Time:    return time;
        case ParamId::Repeats: return repeats;
        case ParamId::Mix:     return mix;
        case ParamId::Filter:  return filter;
        case ParamId::Grit:    return grit;
        case ParamId::ModSpd:  return mod_spd;
        case ParamId::ModDep:  return mod_dep;
        default:               return 0.0f;
    }
}

ParamSet ParamSet::make_default() {
    return ParamSet{
        .time     = 0.5f,
        .repeats  = 0.4f,
        .mix      = 0.5f,
        .filter   = 0.5f,
        .grit     = 0.0f,
        .mod_spd  = 0.5f,
        .mod_dep  = 0.0f,
    };
}

} // namespace delay_fx
} // namespace pedal
