#pragma once

#include "ui/UiModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ardor {

enum class ParameterControlKind {
  Continuous,
  NormalizedChoice,
  Choice,
  Toggle
};

struct ParameterControl {
  std::string key;
  std::string label;
  float minimum = 0.0f;
  float maximum = 0.0f;
  float step = 0.0f;
  float value = 0.0f;
  std::string formatted;
  ParameterControlKind kind = ParameterControlKind::Continuous;
  std::vector<std::string> choices;
  std::vector<float> choiceValues;
  UiSceneScope sceneScope = UiSceneScope::Unavailable;
};

std::vector<ParameterControl> parameterPage(const UiState& state, std::size_t page);
// Up to `count` continuous controls that summarise a block on its chain card.
// Choices are left out: their labels and values do not fit a card. Works for
// any block, selected or not.
std::vector<ParameterControl> blockSummaryControls(const UiBlock& block, std::size_t count);
std::size_t parameterPageCount(const UiState& state);
bool applyParameterDelta(UiState& state, const ParameterControl& control, int delta);

} // namespace ardor
