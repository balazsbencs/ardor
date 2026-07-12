#pragma once

#include "ui/UiModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ardor {

struct ParameterControl {
  std::string key;
  std::string label;
  float minimum = 0.0f;
  float maximum = 0.0f;
  float step = 0.0f;
  float value = 0.0f;
  std::string formatted;
};

std::vector<ParameterControl> parameterPage(const UiState& state, std::size_t page);
std::size_t parameterPageCount(const UiState& state);
bool applyParameterDelta(UiState& state, const ParameterControl& control, int delta);

} // namespace ardor
