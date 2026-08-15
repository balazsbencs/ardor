#include "ui/ParameterControls.h"

#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

// Attack and sustain run either side of zero, so the sign carries meaning and
// has to be shown.
std::string formatSignedPercent(float value)
{
  const int rounded = static_cast<int>(std::lround(value));
  return (rounded > 0 ? "+" : "") + std::to_string(rounded) + "%";
}

std::string formatMilliseconds(float value)
{
  if (std::fabs(value) < 10.0f) {
    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%.1f ms", value);
    return buffer;
  }
  return std::to_string(static_cast<int>(std::lround(value))) + " ms";
}

std::string formatHertz(float value)
{
  return std::to_string(static_cast<int>(std::lround(value))) + " Hz";
}

std::string formatRatio(float value)
{
  char buffer[24]{};
  if (std::fabs(value - std::round(value)) < 0.01f) {
    std::snprintf(buffer, sizeof(buffer), "%.0f:1", value);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1f:1", value);
  }
  return buffer;
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

  const auto* selected = selectedUiBlock(state);
  if (!selected) return {};
  const auto& block = *selected;
  if (block.type == "nam") {
    bool useNano = false;
    const auto explicitPreference = block.params.find("useNano");
    const auto legacyQuality = block.params.find("quality");
    if (explicitPreference != block.params.end() && explicitPreference->is_boolean()) {
      useNano = explicitPreference->get<bool>();
    } else if (legacyQuality != block.params.end() && legacyQuality->is_number()) {
      useNano = legacyQuality->get<float>() == 0.0f;
    }
    const auto inputMode = block.params.value("inputMode", std::string{"sum"});
    const std::size_t inputModeIndex = inputMode == "left" ? 1 : inputMode == "right" ? 2 : 0;
    return {
      choiceControl("inputMode", "Input source", {"L+R Average", "Left / Mono", "Right"},
                    inputModeIndex, ParameterControlKind::Choice),
      choiceControl("useNano", "Use nano model", {"Off", "On"}, useNano ? 1 : 0,
                    ParameterControlKind::Toggle),
    };
  }

  if (block.type == "cab") {
    return {
      control("levelDb", "Level", -60.0f, 12.0f, 1.0f, block.params.value("levelDb", 0.0f), formatDb),
      control("mix", "Mix", 0.0f, 1.0f, 0.05f, block.params.value("mix", 1.0f), formatPercent),
    };
  }

  if (block.type == "wah") {
    return {
      control("position", "Position", 0.0f, 1.0f, 0.01f,
              block.params.value("position", 0.0f), formatPercent),
      control("level", "Level", -24.0f, 24.0f, 1.0f,
              block.params.value("level", 0.0f), formatDb),
    };
  }

  if (block.type == "dualAmp") {
    const auto inputMode = block.params.value("inputMode", std::string{"sum"});
    const std::size_t inputModeIndex = inputMode == "left" ? 1 : inputMode == "right" ? 2 : 0;
    return {
      choiceControl("inputMode", "Input source", {"L+R Average", "Left / Mono", "Right"},
                    inputModeIndex, ParameterControlKind::Choice),
      choiceControl("leftUseNano", "Left nano", {"Off", "On"},
                    block.params.value("leftUseNano", false) ? 1 : 0, ParameterControlKind::Toggle),
      control("leftCabLevelDb", "Left cab level", -60.0f, 12.0f, 1.0f,
              block.params.value("leftCabLevelDb", 0.0f), formatDb),
      control("leftCabMix", "Left cab mix", 0.0f, 1.0f, 0.05f,
              block.params.value("leftCabMix", 1.0f), formatPercent),
      choiceControl("leftPolarityInvert", "Invert left", {"Off", "On"},
                    block.params.value("leftPolarityInvert", false) ? 1 : 0,
                    ParameterControlKind::Toggle),
      choiceControl("rightUseNano", "Right nano", {"Off", "On"},
                    block.params.value("rightUseNano", false) ? 1 : 0, ParameterControlKind::Toggle),
      control("rightCabLevelDb", "Right cab level", -60.0f, 12.0f, 1.0f,
              block.params.value("rightCabLevelDb", 0.0f), formatDb),
      control("rightCabMix", "Right cab mix", 0.0f, 1.0f, 0.05f,
              block.params.value("rightCabMix", 1.0f), formatPercent),
      choiceControl("rightPolarityInvert", "Invert right", {"Off", "On"},
                    block.params.value("rightPolarityInvert", false) ? 1 : 0,
                    ParameterControlKind::Toggle),
    };
  }

  if (block.type == "dualRig") {
    const auto inputMode = block.params.value("inputMode", std::string{"sum"});
    const std::size_t inputModeIndex = inputMode == "left" ? 1 : inputMode == "right" ? 2 : 0;
    return {
      choiceControl("inputMode", "Input source", {"L+R Average", "Left / Mono", "Right"},
                    inputModeIndex, ParameterControlKind::Choice),
      control("leftLevelDb", "Left lane level", -60.0f, 12.0f, 1.0f,
              block.params.value("leftLevelDb", 0.0f), formatDb),
      choiceControl("leftPolarityInvert", "Invert left", {"Off", "On"},
                    block.params.value("leftPolarityInvert", false) ? 1 : 0,
                    ParameterControlKind::Toggle),
      control("rightLevelDb", "Right lane level", -60.0f, 12.0f, 1.0f,
              block.params.value("rightLevelDb", 0.0f), formatDb),
      choiceControl("rightPolarityInvert", "Invert right", {"Off", "On"},
                    block.params.value("rightPolarityInvert", false) ? 1 : 0,
                    ParameterControlKind::Toggle),
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

  if (block.type == "dynamics" && block.params.value("mode", "") == "noise_gate") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    return {
      control("threshold_db", "Threshold", -80.0f, 0.0f, 1.0f,
              number("threshold_db", -55.0f), formatDb),
      control("reduction_db", "Reduction", 0.0f, 96.0f, 1.0f,
              number("reduction_db", 80.0f), formatDb),
      control("attack_ms", "Attack", 0.1f, 50.0f, 0.5f,
              number("attack_ms", 2.0f), formatMilliseconds),
      control("hold_ms", "Hold", 0.0f, 500.0f, 5.0f,
              number("hold_ms", 50.0f), formatMilliseconds),
      control("release_ms", "Release", 10.0f, 2000.0f, 10.0f,
              number("release_ms", 150.0f), formatMilliseconds),
      control("hysteresis_db", "Hysteresis", 0.0f, 18.0f, 1.0f,
              number("hysteresis_db", 6.0f), formatDb),
      control("sidechain_hpf_hz", "Sidechain HPF", 20.0f, 500.0f, 10.0f,
              number("sidechain_hpf_hz", 80.0f), formatHertz),
    };
  }

  if (block.type == "dynamics" && block.params.value("mode", "") == "transient_shaper") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    return {
      control("attack", "Attack", -100.0f, 100.0f, 1.0f, number("attack", 0.0f), formatSignedPercent),
      control("sustain", "Sustain", -100.0f, 100.0f, 1.0f, number("sustain", 0.0f), formatSignedPercent),
      control("mix", "Mix", 0.0f, 1.0f, 0.05f, number("mix", 1.0f), formatPercent),
      control("output_db", "Output", -24.0f, 24.0f, 0.5f, number("output_db", 0.0f), formatDb),
    };
  }

  if (block.type == "distortion" && block.params.value("mode", "") == "rat") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    return {
      control("distortion", "Distortion", 0.0f, 1.0f, 0.01f, number("distortion", 0.5f), formatPercent),
      control("filter", "Filter", 0.0f, 1.0f, 0.01f, number("filter", 0.5f), formatPercent),
      control("volume", "Volume", 0.0f, 1.0f, 0.01f, number("volume", 0.7f), formatPercent),
    };
  }

  if (block.type == "distortion" && block.params.value("mode", "") == "big_cheese") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    return {
      control("fuzz", "Fuzz", 0.0f, 1.0f, 0.01f, number("fuzz", 0.7f), formatPercent),
      control("tone", "Tone", 0.0f, 1.0f, 0.01f, number("tone", 0.5f), formatPercent),
      control("volume", "Volume", 0.0f, 1.0f, 0.01f, number("volume", 0.7f), formatPercent),
    };
  }

  if (block.type == "stereo") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    return {
      control("width", "Width", 0.0f, 2.0f, 0.05f, number("width", 1.0f), formatPercent),
      control("delayMs", "Side delay", 0.0f, 30.0f, 0.5f, number("delayMs", 0.0f), formatMilliseconds),
      control("bassMonoHz", "Bass mono", 0.0f, 500.0f, 10.0f, number("bassMonoHz", 0.0f), formatHertz),
      control("levelDb", "Level", -24.0f, 12.0f, 1.0f, number("levelDb", 0.0f), formatDb),
    };
  }

  if (block.type == "irreverb") {
    const auto number = [&](const char* key, float fallback) { return block.params.value(key, fallback); };
    return {
      control("mix", "Mix", 0.0f, 1.0f, 0.05f, number("mix", 0.35f), formatPercent),
      control("levelDb", "Level", -60.0f, 12.0f, 1.0f, number("levelDb", 0.0f), formatDb),
      control("preDelayMs", "Pre-delay", 0.0f, 500.0f, 1.0f, number("preDelayMs", 0.0f), formatMilliseconds),
      control("lowCutHz", "Low cut", 20.0f, 2000.0f, 10.0f, number("lowCutHz", 20.0f), formatHertz),
      control("highCutHz", "High cut", 500.0f, 20000.0f, 100.0f, number("highCutHz", 20000.0f), formatHertz),
    };
  }

  const auto* descriptor = findDaisyFxDescriptor(block.type, block.params.value("mode", ""));
  if (descriptor == nullptr) {
    return {};
  }

  std::vector<ParameterControl> controls;
  controls.reserve(descriptor->params.size());
  for (const auto& param : descriptor->params) {
    const float value = std::clamp(block.params.value(param.key, param.defaultValue), 0.0f, 1.0f);
    const auto spec = daisyFxParamControlSpec(*descriptor, param);
    if (!spec.choiceValues.empty()) {
      const auto selected = static_cast<std::size_t>(std::distance(spec.choiceValues.begin(),
        std::min_element(spec.choiceValues.begin(), spec.choiceValues.end(), [value](float left, float right) {
          return std::fabs(left - value) < std::fabs(right - value);
        })));
      std::vector<std::string> choices;
      choices.reserve(spec.choiceValues.size());
      for (const float choiceValue : spec.choiceValues) {
        choices.push_back(formatDaisyFxParamValue(*descriptor, param, choiceValue));
      }
      controls.push_back({param.key, param.label, 0.0f,
                          static_cast<float>(choices.size() - 1), 1.0f,
                          static_cast<float>(selected), choices[selected],
                          ParameterControlKind::NormalizedChoice, std::move(choices), spec.choiceValues});
    } else {
      controls.push_back({param.key, param.label, 0.0f, 1.0f, spec.step, value,
                          formatDaisyFxParamValue(*descriptor, param, value)});
    }
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

  const auto* selectedBlock = selectedUiBlock(state);
  if (!selectedBlock) return false;
  if (control.kind != ParameterControlKind::Continuous) {
    const auto selected = static_cast<std::size_t>(std::clamp(
      static_cast<int>(std::lround(control.value)) + delta, 0,
      static_cast<int>(control.choices.size() - 1)));
    if (control.kind == ParameterControlKind::NormalizedChoice) {
      const float before = selectedBlock->params.value(control.key, 0.0f);
      setSelectedBlockParam(state, control.key, control.choiceValues[selected]);
      return selectedUiBlock(state)->params.value(control.key, 0.0f) != before;
    }
    if (control.kind == ParameterControlKind::Toggle) {
      const bool before = selectedBlock->params.value(control.key, false);
      setSelectedBlockParamValue(state, control.key, selected != 0);
      return selectedUiBlock(state)->params.value(control.key, false) != before;
    }
    const std::string before = selectedBlock->params.value(control.key, std::string{});
    if (control.key == "inputMode") {
      constexpr const char* kInputModes[] = {"sum", "left", "right"};
      setSelectedBlockParamValue(state, control.key, kInputModes[std::min<std::size_t>(selected, 2)]);
    } else {
      setSelectedBlockParamValue(state, control.key, selected == 0 ? "peak" : "rms");
    }
    return selectedUiBlock(state)->params.value(control.key, std::string{}) != before;
  }
  const float value = control.value + control.step * static_cast<float>(delta);
  const float before = selectedBlock->params.value(control.key, control.value);
  setSelectedBlockParam(state, control.key, value);
  return selectedUiBlock(state)->params.value(control.key, control.value) != before;
}

} // namespace ardor
