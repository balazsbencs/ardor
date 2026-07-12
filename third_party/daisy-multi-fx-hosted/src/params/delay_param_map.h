#pragma once
#include "param_range.h"
#include "delay_param_id.h"
#include "../config/delay_mode_id.h"

namespace pedal {
namespace delay_fx {

const ParamRange& get_param_range(DelayModeId mode, ParamId param);

namespace default_ranges {
    constexpr ParamRange TIME      = {0.06f,  2.5f, 2.0f};
    constexpr ParamRange TIME_LOFI = {0.002f, 2.5f, 2.0f};
    constexpr ParamRange REPEATS   = {0.0f,  0.98f, 0.0f};
    constexpr ParamRange MIX       = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange FILTER    = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange GRIT      = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange MOD_SPD   = {0.05f, 10.0f, 1.0f};
    constexpr ParamRange MOD_DEP   = {0.0f,   1.0f, 0.0f};
}

} // namespace delay_fx
} // namespace pedal
