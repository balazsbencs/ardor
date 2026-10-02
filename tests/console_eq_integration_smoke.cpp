#include "audio/EngineLoader.h"
#include "audio/WdwRoutingBuilder.h"
#include "equalizer/ConsoleEqProcessor.h"
#include "preset/ScenePlan.h"
#include "ui/ParameterControls.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

#ifndef ARDOR_NAM_EXAMPLE_MODEL
#error "ARDOR_NAM_EXAMPLE_MODEL must point at a loadable NAM model"
#endif

using namespace ardor;
namespace {
void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
ChainBlockPlan consoleBlock(const char* id) {
  ChainBlockPlan block;
  block.id = id;
  block.type = "eq";
  block.status = ChainBlockStatus::Ready;
  block.params = defaultConsoleEqParams();
  return block;
}
ChainBlockPlan namBlock(const char* id) {
  ChainBlockPlan block;
  block.id = id;
  block.type = "nam";
  block.status = ChainBlockStatus::Ready;
  block.assetPath = ARDOR_NAM_EXAMPLE_MODEL;
  return block;
}
std::size_t peakIndex(const std::array<float, 64>& samples) {
  return static_cast<std::size_t>(std::distance(samples.begin(), std::max_element(samples.begin(), samples.end())));
}

// Preset planning and both real engine loading paths must use the console
// processor rather than silently interpreting it as a five-band EQ.
void testEngineAndLane(Preset& preset, RuntimeChain& lane) {
  const auto defaults = defaultConsoleEqParams();
  std::string error;
  preset.blocks = {{"console", "eq", true, "", defaults}};
  const auto plan = buildChainPlan(preset, {});
  require(plan.runnableBlockCount == 1 && plan.blocks[0].params == defaults, "plan preserves console EQ parameters");
  PedalEngine engine;
  require(applyChainPlan(engine, plan, {}, error), error);
  require(engine.setConsoleEqParameter("console", "high_db", 6), "engine live update");
  require(!engine.setConsoleEqParameter("missing", "high_db", 6), "unknown block rejected");
  require(prepareRuntimeChain(lane, plan.blocks, {}, error), error);
  lane.prepareBlockSize(64);
  std::array<float, 64> input{}, left{}, right{};
  input[0] = .1f;
  lane.processBlock(input.data(), left.data(), right.data(), input.size());
  require(peakIndex(left) == 15 && left == right, "mono wet impulse latency is 15 frames on both outputs");
  require(lane.setBlockEnabled("console", false), "console EQ bypass works");
  for (int i = 0; i < 100; ++i) lane.processBlock(input.data(), left.data(), right.data(), input.size());
  require(peakIndex(left) == 15, "bypass keeps the same latency");
}

void testNestedDualRig() {
  ChainPlan plan;
  ChainBlockPlan rig;
  rig.id = "rig";
  rig.type = "dualRig";
  rig.status = ChainBlockStatus::Ready;
  rig.params = {{"inputMode", "sum"}, {"leftLevelDb", 0.0f}, {"leftPolarityInvert", false},
                {"rightLevelDb", 0.0f}, {"rightPolarityInvert", false}};
  rig.lanes[1].push_back(consoleBlock("rig-eq"));
  plan.blocks.push_back(std::move(rig));
  PedalEngine engine;
  std::string error;
  require(applyChainPlan(engine, plan, {48000, 64, 8192}, error), error);
  require(engine.setConsoleEqParameter("rig-eq", "mid_db", 3), "live update reaches a Dual Rig lane");
  require(!engine.setConsoleEqParameter("rig-eq", "unknown", 3), "nested unknown key rejected");
}

void testWdwDryLane() {
  ChainPlan dry, wet;
  dry.blocks.push_back(namBlock("dry-nam"));
  dry.blocks.push_back(consoleBlock("dry-eq"));
  wet.blocks.push_back(namBlock("wet-nam"));
  WdwRoutingBuildOptions options;
  options.engine.blockSize = 16;
  options.engine.sampleRate = 48000;
  options.engine.irSamples = 8192;
  options.program.executor.mode = WdwPairExecutionMode::Direct;
  options.program.executor.requireWorkerSetup = false;
  options.program.executor.requireRealtimeScheduling = false;
  options.program.executor.requireAffinity = false;
  options.calibrateLatencies = false;
  std::unique_ptr<WdwRoutingProgram> program;
  WdwRoutingBuildReport report;
  std::string error;
  require(buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
  require(program->setConsoleEqParameter("dry-eq", "low_db", 4), "live update reaches a WDW dry lane");
  require(!program->setConsoleEqParameter("dry-nam", "low_db", 4), "WDW rejects a non-EQ block");
}

void testUi() {
  const auto defaults = defaultConsoleEqParams();
  auto state = makeDemoUiState();
  auto asset = std::find_if(state.assets.begin(), state.assets.end(), [](const auto& a) { return a.mode == kConsoleEqMode; });
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
  const auto second = parameterPage(state, 1);
  require(second.size() == 5 && second[1].label == "Character" && second[1].formatted == "Console",
          "pedal shows the Character switch next to Saturation");
  auto corrupt = *selectedUiBlock(state);
  corrupt.params["mid_db"] = "corrupt";
  require(blockSummaryControls(corrupt, kConsoleEqControls.size())[2].formatted == "+0.0 dB", "a corrupt stored value shows the default");
}

void testScenes(Preset& preset, RuntimeChain& lane) {
  std::string error;
  PresetSceneSet scenes;
  for (auto& scene : scenes.scenes) {
    scene.targets = {{PresetSceneTargetType::Parameter, "console", "high_db", "", 6.f},
                     {PresetSceneTargetType::Parameter, "console", "mid_freq", "", 4.f},
                     {PresetSceneTargetType::Parameter, "console", "character", "", 0.f}};
  }
  preset.sceneSet = scenes;
  ScenePlan scenePlan;
  require(buildScenePlan(preset, scenePlan, error), error);
  const auto program = makeSceneTransitionProgram(scenePlan, 1);
  require(program.targets.size() == 3 && program.targets[1].law == SceneTransitionLaw::Stepped
            && program.targets[2].law == SceneTransitionLaw::Stepped,
          "scene frequency and Character controls are stepped");
  require(lane.applySceneTarget(program.targets[0].address, 8), "scene gain reaches processor");
  require(lane.applySceneTarget(program.targets[1].address, 5), "scene frequency reaches processor");
  require(lane.applySceneTarget(program.targets[2].address, 0), "scene Character reaches processor");
}
}

int main() {
  Preset preset;
  RuntimeChain lane;
  testEngineAndLane(preset, lane);
  testNestedDualRig();
  testWdwDryLane();
  testUi();
  testScenes(preset, lane);
  std::cout << "1073 EQ presets, engines, Dual Rig, WDW, pedal UI and scenes passed\n";
}
