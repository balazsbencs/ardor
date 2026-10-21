// SPDX-License-Identifier: MIT
#pragma once
#include "parametric_control.h"
#include "NAM/dsp.h"
namespace nam::ardor_film {
std::unique_ptr<DSP> create(const nlohmann::json& document);
}
