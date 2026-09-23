#include "preset/ScenePlan.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << "scene plan smoke failed: " << message << '\n';
    std::exit(1);
  }
}

ardor::Preset makePreset()
{
  ardor::Preset preset;
  preset.version = 4;
  preset.name = "Scene plan";
  preset.blocks = {
    {"drive", "distortion", true, "", {{"mode", "rat"}, {"distortion", 0.5f}}},
    {"echo", "delay", true, "", {{"mode", "digital"}, {"mix", 0.2f}}},
  };
  preset.blocks[1].sceneBypass = ardor::PresetSceneBypassPolicy::LetRing;
  ardor::PresetSceneSet set;
  set.defaultSceneId = "b";
  for (std::size_t index = 0; index < set.scenes.size(); ++index) {
    auto& scene = set.scenes[index];
    scene.id = std::string(1, static_cast<char>('a' + index));
    scene.name = "Scene " + std::to_string(index + 1);
    scene.enterTimeMs = index == 0 ? 0 : 500;
    scene.outputTrimDb = static_cast<float>(index) - 1.5f;
    scene.targets = {
      {ardor::PresetSceneTargetType::InputGainDb, "", "", "", -6.0f + index},
      {ardor::PresetSceneTargetType::Parameter, "drive", "distortion", "", 0.2f * index},
      {ardor::PresetSceneTargetType::BlockEnabled, "echo", "", "", index % 2 == 0},
    };
  }
  preset.sceneSet = std::move(set);
  return preset;
}

} // namespace

int main()
{
  auto preset = makePreset();
  ardor::ScenePlan plan;
  std::string error;
  require(ardor::buildScenePlan(preset, plan, error), error);
  require(plan.defaultSceneIndex == 1, "default scene was not resolved");
  require(plan.targets.size() == 3, "target count changed");
  require(plan.targets[0].transition == ardor::SceneTransitionLaw::Decibels,
          "input gain should interpolate in decibels");
  require(plan.targets[1].kind == ardor::SceneRuntimeTargetKind::DistortionParameter,
          "distortion target was not classified");
  require(plan.targets[1].location.topIndex == 0 && !plan.targets[1].location.child,
          "block path was not resolved");
  require(plan.targets[2].transition == ardor::SceneTransitionLaw::Stepped,
          "enabled target should be stepped");
  require(plan.targets[2].bypass == ardor::SceneBypassCapability::TailCapable,
          "delay bypass should advertise tail capability");
  require(plan.admission.preparedProcessors == 2
            && plan.admission.maximumSteadyStateProcessors == 2
            && plan.admission.maximumTransitionProcessors == 2
            && plan.admission.maximumRetainedTails == 1,
          "let-ring admission accounting changed");
  auto reordered = makePreset();
  std::swap(reordered.sceneSet->scenes[1].targets[0], reordered.sceneSet->scenes[1].targets[1]);
  require(ardor::buildScenePlan(reordered, plan, error),
          "scene target order should not change address matching");
  require(plan.targets[0].values[1] == -5.0f && plan.targets[1].values[1] == 0.2f,
          "reordered scene values were assigned to the wrong addresses");
  require(ardor::admitScenePlan(plan, 2, 2, error), error);
  const auto transitionProgram = ardor::makeSceneTransitionProgram(plan, 17);
  require(transitionProgram.presetGeneration == 17
            && transitionProgram.defaultSceneIndex == 1
            && transitionProgram.targets.size() == plan.targets.size(),
          "transition program did not preserve prepared scene metadata");

  auto withSkippedBlock = makePreset();
  withSkippedBlock.blocks.insert(withSkippedBlock.blocks.begin(),
    {"shared-off", "distortion", false, "", {{"mode", "rat"}}});
  require(ardor::buildScenePlan(withSkippedBlock, plan, error), error);
  require(plan.targets[1].location.topIndex == 0
            && plan.targets[2].location.topIndex == 1,
          "scene paths must index the prepared runtime chain, excluding shared disabled blocks");

  for (auto& scene : preset.sceneSet->scenes) scene.targets[1].value = 2.0f;
  require(!ardor::buildScenePlan(preset, plan, error), "out-of-range parameter was accepted");
  require(error.find("out of range") != std::string::npos, "range failure was not explained");

  preset = makePreset();
  for (auto& scene : preset.sceneSet->scenes) {
    scene.targets[1].blockId = "echo";
    scene.targets[1].parameter = "time";
  }
  require(!ardor::buildScenePlan(preset, plan, error), "delay time was incorrectly scene-capable");
  require(error.find("not scene-capable") != std::string::npos,
          "unsupported target failure was not explained");

  preset = makePreset();
  preset.blocks[1].type = "cab";
  for (auto& scene : preset.sceneSet->scenes) scene.targets.erase(scene.targets.begin() + 1);
  require(!ardor::buildScenePlan(preset, plan, error),
          "structural cabinet bypass was incorrectly scene-capable");
  require(error.find("shared-only") != std::string::npos,
          "structural bypass rejection was not explained");

  auto tailHeavy = makePreset();
  tailHeavy.blocks.push_back(
    {"echo-2", "delay", false, "", {{"mode", "digital"}, {"mix", 0.2f}}});
  tailHeavy.blocks.push_back(
    {"echo-3", "delay", false, "", {{"mode", "digital"}, {"mix", 0.2f}}});
  tailHeavy.blocks[2].sceneBypass = ardor::PresetSceneBypassPolicy::LetRing;
  tailHeavy.blocks[3].sceneBypass = ardor::PresetSceneBypassPolicy::LetRing;
  for (std::size_t scene = 0; scene < tailHeavy.sceneSet->scenes.size(); ++scene) {
    tailHeavy.sceneSet->scenes[scene].targets[2].value = scene == 0;
    tailHeavy.sceneSet->scenes[scene].targets.push_back(
      {ardor::PresetSceneTargetType::BlockEnabled, "echo-2", "", "", scene == 1});
    tailHeavy.sceneSet->scenes[scene].targets.push_back(
      {ardor::PresetSceneTargetType::BlockEnabled, "echo-3", "", "", scene == 2});
  }
  require(ardor::buildScenePlan(tailHeavy, plan, error), error);
  require(plan.admission.preparedProcessors == 4,
          "disabled scene-addressable effects must reserve processor state");
  require(plan.admission.maximumSteadyStateProcessors == 2,
          "steady-state accounting should follow each scene's enabled values");
  require(plan.admission.maximumRetainedTails == 3
            && plan.admission.maximumTransitionProcessors == 4,
          "repeated recalls must reserve all reachable let-ring tails");
  require(!ardor::admitScenePlan(plan, 3, 4, error),
          "prepared processor state limit was not enforced");
  require(error.find("processor states") != std::string::npos,
          "prepared-state rejection was not explained");
  require(!ardor::admitScenePlan(plan, 4, 3, error),
          "tail-overlap render limit was not enforced");
  require(error.find("retained tails") != std::string::npos,
          "tail-overlap rejection was not explained");

  std::cout << "scene plan smoke passed\n";
  return 0;
}
