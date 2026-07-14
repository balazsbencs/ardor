#include "ui/ParameterControls.h"

#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr std::size_t kControlsPerPage = 7;

std::string formatDb(float value)
{
  return std::to_string(static_cast<int>(std::lround(value))) + " dB";
}

std::string formatPercent(float value)
{
  return std::to_string(static_cast<int>(std::lround(value * 100.0f))) + "%";
}

std::string formatMilliseconds(float value)
{
  return std::to_string(static_cast<int>(std::lround(value))) + " ms";
}

std::string formatHertz(float value)
{
  return std::to_string(static_cast<int>(std::lround(value))) + " Hz";
}

std::string formatRatio(float value)
{
  return std::to_string(static_cast<int>(std::lround(value))) + ":1";
}

ParameterControl control(std::string key, std::string label, float minimum, float maximum, float step,
                         float value, std::string (*format)(float))
{
  value = std::clamp(value, minimum, maximum);
  return {std::move(key), std::move(label), minimum, maximum, step, value, format(value)};
}

ParameterControl choiceControl(std::string key, std::string label, std::vector<std::string> choices,
                               std::size_t selected, ParameterControlKind kind)
{
  selected = std::min(selected, choices.size() - 1);
  return {std::move(key), std::move(label), 0.0f, static_cast<float>(choices.size() - 1), 1.0f,
          static_cast<float>(selected), choices[selected], kind, std::move(choices)};
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

  if (block.type == "dynamics" && block.params.value("mode", "") == "compressor") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    const auto detector = block.params.value("detector", std::string{"peak"});
    return {
      control("threshold_db", "Threshold", -60.0f, 0.0f, 1.0f, number("threshold_db", -24.0f), formatDb),
      control("ratio", "Ratio", 1.0f, 20.0f, 0.5f, number("ratio", 4.0f), formatRatio),
      control("attack_ms", "Attack", 0.1f, 200.0f, 1.0f, number("attack_ms", 10.0f), formatMilliseconds),
      control("release_ms", "Release", 10.0f, 2000.0f, 10.0f, number("release_ms", 150.0f), formatMilliseconds),
      control("knee_db", "Knee", 0.0f, 24.0f, 1.0f, number("knee_db", 6.0f), formatDb),
      control("makeup_db", "Makeup", 0.0f, 24.0f, 1.0f, number("makeup_db", 0.0f), formatDb),
      control("input_gain_db", "Input", -24.0f, 24.0f, 1.0f, number("input_gain_db", 0.0f), formatDb),
      control("mix", "Mix", 0.0f, 1.0f, 0.05f, number("mix", 1.0f), formatPercent),
      control("sidechain_hpf_hz", "Sidechain HPF", 20.0f, 500.0f, 10.0f,
              number("sidechain_hpf_hz", 80.0f), formatHertz),
      choiceControl("detector", "Detector", {"Peak", "RMS"}, detector == "rms" ? 1 : 0,
                    ParameterControlKind::Choice),
      choiceControl("auto_makeup", "Auto Makeup", {"Off", "On"},
                    block.params.value("auto_makeup", false) ? 1 : 0, ParameterControlKind::Toggle),
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

  if (state.paramTarget == UiParamTarget::Globals) {
    const float value = control.value + control.step * static_cast<float>(delta);
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
  if (control.kind != ParameterControlKind::Continuous) {
    const auto selected = static_cast<std::size_t>(std::clamp(
      static_cast<int>(std::lround(control.value)) + delta, 0,
      static_cast<int>(control.choices.size() - 1)));
    if (control.kind == ParameterControlKind::Toggle) {
      const bool before = blocks[state.selectedBlock].params.value(control.key, false);
      setSelectedBlockParamValue(state, control.key, selected != 0);
      return blocks[state.selectedBlock].params.value(control.key, false) != before;
    }
    const std::string before = blocks[state.selectedBlock].params.value(control.key, std::string{});
    setSelectedBlockParamValue(state, control.key, selected == 0 ? "peak" : "rms");
    return blocks[state.selectedBlock].params.value(control.key, std::string{}) != before;
  }
  const float value = control.value + control.step * static_cast<float>(delta);
  const float before = blocks[state.selectedBlock].params.value(control.key, control.value);
  setSelectedBlockParam(state, control.key, value);
  return blocks[state.selectedBlock].params.value(control.key, control.value) != before;
}

} // namespace ardor
