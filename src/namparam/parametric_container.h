// SPDX-License-Identifier: MIT
#pragma once
#include "NAM/dsp.h"
#include "parametric_control.h"
namespace nam::ardor_parametric {
std::unique_ptr<DSP> create(const nlohmann::json& document);
}
