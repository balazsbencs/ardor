#pragma once
#include "param_range.h"
#include "reverb_param_id.h"
#include "../config/reverb_mode_id.h"

namespace pedal {
namespace reverb_fx {

const ParamRange& get_param_range(ReverbModeId mode, ParamId param);

struct AlgoParamDescriptor {
    const char* param1_name;
    const char* param2_name;
};

const AlgoParamDescriptor& get_algo_param_descriptor(ReverbModeId mode);

namespace default_ranges {
    constexpr ParamRange DECAY          = {0.5f,  20.0f,  2.0f};
    constexpr ParamRange DECAY_ROOM     = {0.2f,  20.0f,  2.0f};
    constexpr ParamRange DECAY_SPRING   = {0.8f,  10.0f,  2.0f};
    constexpr ParamRange DECAY_CLOUD    = {1.0f,  50.0f,  2.0f};
    constexpr ParamRange DECAY_MAGNETO  = {0.2f,   1.5f,  1.0f};
    constexpr ParamRange DECAY_NONLIN   = {0.05f,  2.0f,  1.0f};
    constexpr ParamRange DECAY_REFL     = {0.133f, 0.4f,  0.0f};
    constexpr ParamRange PRE_DELAY      = {0.0f,   0.5f,  0.0f};
    constexpr ParamRange FEEDBACK       = {0.0f,  0.95f,  0.0f};
    constexpr ParamRange MIX            = {0.0f,   1.0f,  0.0f};
    constexpr ParamRange TONE           = {0.0f,   1.0f,  0.0f};
    constexpr ParamRange MOD            = {0.0f,   1.0f,  0.0f};
    constexpr ParamRange PARAM1         = {0.0f,   1.0f,  0.0f};
    constexpr ParamRange PARAM2         = {0.0f,   1.0f,  0.0f};
    constexpr ParamRange PARAM1_SWELL   = {0.08f,  4.0f,  1.0f};
    constexpr ParamRange PARAM1_SHIMMER = {-12.0f, 24.0f, 0.0f};
    constexpr ParamRange PARAM2_SHIMMER = {-12.0f, 24.0f, 0.0f};
    constexpr ParamRange PARAM1_CHORALE  = {0.0f,  6.0f,  0.0f};
    constexpr ParamRange PARAM2_CHORALE  = {0.0f,  1.0f,  0.0f};
    constexpr ParamRange PARAM1_NONLIN   = {0.0f,  1.0f,  0.0f};
    constexpr ParamRange PARAM2_NONLIN   = {0.0f,  1.0f,  0.0f};
    constexpr ParamRange PARAM1_MAGNETO  = {1.0f,  6.0f,  0.0f};
    constexpr ParamRange PARAM2_MAGNETO  = {0.0f,  1.0f,  0.0f};
    constexpr ParamRange PARAM1_REFL     = {0.0f,  1.0f,  0.0f};
    constexpr ParamRange PARAM2_REFL     = {0.0f,  1.0f,  0.0f};
    constexpr ParamRange PARAM1_BLOOM    = {0.5f,  5.0f,  1.0f};
    constexpr ParamRange PARAM2_BLOOM    = {0.0f,  0.7f,  0.0f};
    constexpr ParamRange DECAY_BLOOM     = {1.0f, 30.0f,  2.0f};
    constexpr ParamRange DECAY_SWELL_R   = {0.5f, 20.0f,  2.0f};
}

} // namespace reverb_fx
} // namespace pedal
