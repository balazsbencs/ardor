#include "delay_param_map.h"

namespace pedal {
namespace delay_fx {

const ParamRange& get_param_range(DelayModeId mode, ParamId param) {
    if (param == ParamId::Time) {
        switch (mode) {
            case DelayModeId::Lofi: return default_ranges::TIME_LOFI;
            default:                return default_ranges::TIME;
        }
    }
    switch (param) {
        case ParamId::Repeats: return default_ranges::REPEATS;
        case ParamId::Mix:     return default_ranges::MIX;
        case ParamId::Filter:  return default_ranges::FILTER;
        case ParamId::Grit:    return default_ranges::GRIT;
        case ParamId::ModSpd:  return default_ranges::MOD_SPD;
        case ParamId::ModDep:  return default_ranges::MOD_DEP;
        default:               return default_ranges::MIX;
    }
}

} // namespace delay_fx
} // namespace pedal
