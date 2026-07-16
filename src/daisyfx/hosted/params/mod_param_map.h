#pragma once
#include "param_range.h"
#include "mod_param_id.h"
#include "../config/mod_mode_id.h"

namespace pedal {
namespace mod_fx {

const ParamRange& get_param_range(ModModeId mode, ParamId param);

namespace default_ranges {
    constexpr ParamRange SPEED = {0.05f, 10.0f, 1.0f};
    constexpr ParamRange DEPTH = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange MIX   = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange TONE  = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange P1    = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange P2    = {0.0f,   1.0f, 0.0f};
    constexpr ParamRange LEVEL = {0.0f,   2.0f, 0.0f};
}

} // namespace mod_fx
} // namespace pedal
