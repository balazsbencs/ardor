#include "equalizer/ConsoleEqProcessor.h"
#include "audio/EngineLoader.h"
#include "preset/ScenePlan.h"
#include "ui/ParameterControls.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace ardor;
namespace {
void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
float response(nlohmann::json params, float hz, float rate = 48000) {
  ConsoleEqProcessor processor;
  std::string error;
  require(processor.configure(params, rate, error), error);
  double sum = 0;
  const int frames = static_cast<int>(rate);
  for (int i = 0; i < frames; ++i) {
    const float x = 0.001f * std::sin(2 * std::numbers::pi * hz * i / rate);
    const auto y = processor.process({x, 0});
    require(std::isfinite(y.left) && y.right == 0, "finite output and no stereo crosstalk");
    if (i >= frames / 2) sum += y.left * y.left;
  }
  return 20 * std::log10(std::sqrt(sum / (frames / 2)) / (0.001 / std::sqrt(2.0)));
}
float relative(nlohmann::json params, float hz) {
  return response(params, hz) - response(defaultConsoleEqParams(), hz);
}
}

int main() {
  const auto defaults = defaultConsoleEqParams();
  for (float rate : {32000.f, 44100.f, 48000.f, 96000.f, 192000.f})
    require(std::abs(response(defaults, 1000, rate)) < 0.1, "neutral default response");
  for (const auto& [key, freq, gain] : std::array<std::tuple<const char*, float, float>, 3>{{
      {"low_db", 5, 16}, {"mid_db", 1600, 18}, {"high_db", 19000, 16}}}) {
    auto params = defaults;
    params[key] = gain;
    const float boost = relative(params, freq);
    params[key] = -gain;
    const float cut = relative(params, freq);
    require(boost > gain - 2 && boost < gain + 0.2f, "band reaches expected boost: " + std::string(key));
    require(std::abs(boost + cut) < 0.15f, "boost/cut curves are reciprocal");
  }
  for (int band = 1; band <= 4; ++band) {
    constexpr float hz[] = {0, 35, 60, 110, 220};
    auto params = defaults;
    params["low_freq"] = band;
    params["low_db"] = 12;
    require(std::abs(relative(params, hz[band]) - 6) < 0.1, "all four low shelf corners");
  }
  for (int band = 1; band <= 6; ++band) {
    constexpr float hz[] = {0, 360, 700, 1600, 3200, 4800, 7200};
    auto params = defaults;
    params["mid_freq"] = band;
    params["mid_db"] = 12;
    require(std::abs(relative(params, hz[band]) - 12) < 0.1, "all six mid frequency positions");
  }
  for (int band = 1; band <= 4; ++band) {
    constexpr float hz[] = {0, 50, 80, 160, 300};
    auto params = defaults;
    params["high_pass"] = band;
    require(std::abs(relative(params, hz[band]) + 3.0103f) < 0.15f, "Butterworth cutoff is -3 dB");
    const float a = relative(params, hz[band] / 4), b = relative(params, hz[band] / 8);
    require(std::abs(a - b - 18.0618f) < 0.15f, "HPF rolls off at 18 dB/octave");
  }
  auto disabled = defaults;
  disabled["low_freq"] = disabled["mid_freq"] = 0;
  disabled["low_db"] = 16;
  disabled["mid_db"] = 18;
  require(std::abs(relative(disabled, 360)) < 0.01f, "Off disables the whole band");

  std::string error;
  ConsoleEqProcessor dry;
  auto params = defaults;
  params["mix"] = 0;
  params["saturation"] = 1;
  params["high_db"] = 16;
  require(dry.configure(params, 48000, error), error);
  std::array<float, ConsoleEqProcessor::kLatencyFrames> history{};
  for (int i = 0; i < 1000; ++i) {
    const float x = std::sin(i * .12f);
    const auto y = dry.process({x, x * .5f});
    require(y.left == history[i % history.size()] && y.right == y.left * .5f, "dry mix is exact and latency aligned");
    history[i % history.size()] = x;
  }
  ConsoleEqProcessor normal, inverted, driven;
  require(normal.configure(defaults, 48000, error), error);
  params = defaults; params["polarity"] = 1;
  require(inverted.configure(params, 48000, error), error);
  params = defaults; params["saturation"] = 1;
  require(driven.configure(params, 48000, error), error);
  double cleanEnergy = 0, drivenEnergy = 0;
  for (int i = 0; i < 4800; ++i) {
    const float x = .5f * std::sin(i * .12f);
    const auto a = normal.process({x, -x}), b = inverted.process({x, -x}), c = driven.process({x, -x});
    require(a.left == -b.left && a.right == -b.right, "polarity inverts the wet path");
    cleanEnergy += a.left * a.left; drivenEnergy += c.left * c.left;
  }
  require(drivenEnergy < cleanEnergy * .1, "saturation compresses large signals");
  require(normal.setParameterTarget("low_db", 100), "valid controls clamp");
  require(!normal.setParameterTarget("bad", 0) && !normal.setParameterTarget("mix", NAN), "invalid live controls rejected");
  for (int i = 0; i < 16000; ++i) {
    if (i % 47 == 0) {
      for (std::size_t j = 0; j < kConsoleEqControls.size(); ++j) {
        const auto& c = kConsoleEqControls[j];
        require(normal.setParameterTarget(c.key, (i / 47 + j) % 2 ? c.minimum : c.maximum), "automation supported");
      }
    }
    const auto y = normal.process({.1f * std::sin(i * .17f), .1f * std::cos(i * .07f)});
    require(std::isfinite(y.left) && std::isfinite(y.right) && std::abs(y.left) < 200, "rapid switching remains stable");
  }
  normal.reset();
  for (int i = 0; i < 2000; ++i) {
    const auto y = normal.process({0,0});
    require(y.left == 0 && y.right == 0, "reset clears tails and silence has no added noise");
  }
  require(!normal.configure(defaults, 0, error), "invalid sample rate rejected");
  params = defaults; params["low_db"] = "bad";
  require(!normal.configure(params, 48000, error), "invalid parameter types rejected");
  params["low_db"] = std::numeric_limits<float>::infinity();
  require(!normal.configure(params, 48000, error), "non-finite config rejected");

  // Verify that preset planning and both real engine loading paths use the new
  // processor rather than silently interpreting it as a five-band EQ.
  Preset preset;
  preset.blocks = {{"console", "eq", true, "", defaults}};
  auto plan = buildChainPlan(preset, {});
  require(plan.runnableBlockCount == 1 && plan.blocks[0].params == defaults, "plan preserves console EQ parameters");
  PedalEngine engine;
  require(applyChainPlan(engine, plan, {}, error), error);
  require(engine.setConsoleEqParameter("console", "high_db", 6), "engine live update");
  require(!engine.setConsoleEqParameter("missing", "high_db", 6), "unknown block rejected");
  RuntimeChain lane;
  require(prepareRuntimeChain(lane, plan.blocks, {}, error), error);
  lane.prepareBlockSize(64);
  std::array<float, 64> input{}, left{}, right{};
  input[0] = .1f;
  lane.processBlock(input.data(), left.data(), right.data(), input.size());
  require(std::distance(left.begin(), std::max_element(left.begin(), left.end())) == 15, "wet impulse latency is 15 frames");
  require(lane.setBlockEnabled("console", false), "console EQ bypass works");
  for (int i = 0; i < 100; ++i) lane.processBlock(input.data(), left.data(), right.data(), input.size());
  require(std::distance(left.begin(), std::max_element(left.begin(), left.end())) == 15, "bypass keeps the same latency");

  auto state = makeDemoUiState();
  auto asset = std::find_if(state.assets.begin(), state.assets.end(), [](const auto& a) {return a.mode == "console_1073";});
  require(asset != state.assets.end() && asset->type == "utility", "pedal browser exposes Utility EQ");
  appendAssetBlock(state, std::distance(state.assets.begin(), asset));
  require(selectedUiBlock(state)->params == defaults, "pedal adds correct defaults");
  state.paramTarget = UiParamTarget::Block;
  const auto controls = parameterPage(state, 0);
  require(controls.size() == 6 && controls[1].formatted == "60 Hz", "pedal frequency labels");
  require(applyParameterDelta(state, controls[0], 1), "pedal half-dB gain step is editable");
  require(parameterPage(state, 0)[0].formatted == "+0.5 dB", "pedal displays half-dB precision");
  require(applyParameterDelta(state, controls[1], 1), "pedal frequency switch is editable");
  require(selectedUiBlock(state)->params["low_freq"] == 3, "switch stores numeric index");

  PresetSceneSet scenes;
  for (auto& scene : scenes.scenes) {
    scene.targets = {{PresetSceneTargetType::Parameter, "console", "high_db", "", 6.f},
                     {PresetSceneTargetType::Parameter, "console", "mid_freq", "", 4.f}};
  }
  preset.sceneSet = scenes;
  ScenePlan scenePlan;
  require(buildScenePlan(preset, scenePlan, error), error);
  const auto program = makeSceneTransitionProgram(scenePlan, 1);
  require(program.targets.size() == 2 && program.targets[1].law == SceneTransitionLaw::Stepped, "scene frequency controls are stepped");
  require(lane.applySceneTarget(program.targets[0].address, 8), "scene gain reaches processor");
  require(lane.applySceneTarget(program.targets[1].address, 5), "scene frequency reaches processor");
  std::cout << "1073 EQ response, automation, presets, live controls and latency passed\n";
}
