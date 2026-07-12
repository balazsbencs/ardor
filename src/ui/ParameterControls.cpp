#include "ui/ParameterControls.h"

#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr std::size_t kControlsPerPage = 6;

std::string formatDb(float value)
{
  return std::to_string(static_cast<int>(std::lround(value))) + " dB";
}

std::string formatPercent(float value)
{
  return std::to_string(static_cast<int>(std::lround(value * 100.0f))) + "%";
}

ParameterControl control(std::string key, std::string label, float minimum, float maximum, float step,
                         float value, std::string (*format)(float))
{
  value = std::clamp(value, minimum, maximum);
  return {std::move(key), std::move(label), minimum, maximum, step, value, format(value)};
}

std::vector<ParameterControl> controlsFor(const UiState& state)
{
  if (state.paramTarget == UiParamTarget::Globals) {
    const auto& global = state.bank.presets[state.activePreset].global;
    return {
      control("inputGainDb", "Input", -60.0f, 12.0f, 1.0f, global.inputGainDb, formatDb),
      control("outputGainDb", "Output", -60.0f, 12.0f, 1.0f, global.outputGainDb, formatDb),
    };
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return {};
  }

  const auto& block = blocks[state.selectedBlock];
  if (block.type == "cab") {
    return {
      control("levelDb", "Level", -60.0f, 12.0f, 1.0f, block.params.value("levelDb", 0.0f), formatDb),
      control("mix", "Mix", 0.0f, 1.0f, 0.05f, block.params.value("mix", 1.0f), formatPercent),
    };
  }

  const auto* descriptor = findDaisyFxDescriptor(block.type, block.params.value("mode", ""));
  if (descriptor == nullptr) {
    return {};
  }

  std::vector<ParameterControl> controls;
  controls.reserve(descriptor->params.size());
  for (const auto& param : descriptor->params) {
    controls.push_back(control(param.key, param.label, 0.0f, 1.0f, 0.05f,
                               block.params.value(param.key, param.defaultValue), formatPercent));
  }
  return controls;
}

} // namespace

std::vector<ParameterControl> parameterPage(const UiState& state, std::size_t page)
{
  const auto controls = controlsFor(state);
  const std::size_t first = page * kControlsPerPage;
  if (first >= controls.size()) {
    return {};
  }
  const std::size_t last = std::min(first + kControlsPerPage, controls.size());
  return {controls.begin() + static_cast<std::ptrdiff_t>(first),
          controls.begin() + static_cast<std::ptrdiff_t>(last)};
}

std::size_t parameterPageCount(const UiState& state)
{
  const auto count = controlsFor(state).size();
  return (count + kControlsPerPage - 1) / kControlsPerPage;
}

bool applyParameterDelta(UiState& state, const ParameterControl& control, int delta)
{
  if (delta == 0) {
    return false;
  }

  const float value = control.value + control.step * static_cast<float>(delta);
  if (state.paramTarget == UiParamTarget::Globals) {
    const auto& global = state.bank.presets[state.activePreset].global;
    if (control.key == "inputGainDb") {
      const float before = global.inputGainDb;
      setActiveInputGainDb(state, value);
      return global.inputGainDb != before;
    }
    if (control.key == "outputGainDb") {
      const float before = global.outputGainDb;
      setActiveOutputGainDb(state, value);
      return global.outputGainDb != before;
    }
    return false;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return false;
  }
  const float before = blocks[state.selectedBlock].params.value(control.key, control.value);
  setSelectedBlockParam(state, control.key, value);
  return blocks[state.selectedBlock].params.value(control.key, control.value) != before;
}

} // namespace ardor
