#include "dsp/SceneTransition.h"
#include "dsp/PedalEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << "scene transition smoke failed: " << message << '\n';
    std::exit(1);
  }
}

bool near(float actual, float expected, float tolerance = 0.0001f)
{
  return std::fabs(actual - expected) <= tolerance;
}

ardor::SceneTransitionProgram program()
{
  ardor::SceneTransitionProgram result;
  result.presetGeneration = 42;
  result.defaultSceneIndex = 0;
  result.outputTrimDb = {0.0f, 6.0f, -6.0f, 0.0f};
  result.targets = {
    {ardor::SceneTransitionLaw::Linear, {0.0f, 1.0f, 0.25f, 0.0f}},
    {ardor::SceneTransitionLaw::LogFrequency, {100.0f, 1600.0f, 400.0f, 100.0f}},
    {ardor::SceneTransitionLaw::Stepped, {0.0f, 1.0f, 0.0f, 0.0f}},
  };
  return result;
}

} // namespace

int main()
{
  ardor::SceneTransitionController controller;
  require(controller.prepare(program()), "valid program should prepare");
  require(controller.currentValues().size() == 3, "prepared target count changed");
  require(near(controller.currentValues()[1], 100.0f), "default scene was not installed");

  require(controller.request({42, 1, 4, 1}), "timed request should publish");
  controller.beginBlock();
  require(near(controller.currentValues()[0], 0.0f), "continuous target stepped at recall");
  require(near(controller.currentValues()[2], 1.0f), "stepped target did not apply at recall");
  controller.advanceFrame();
  require(near(controller.currentValues()[0], 0.25f), "linear quarter point is wrong");
  require(near(controller.currentValues()[1], 200.0f, 0.01f), "logarithmic quarter point is wrong");
  require(near(controller.currentOutputTrimDb(), 1.5f), "trim quarter point is wrong");

  // A new transition snapshots the rendered quarter-point values rather than
  // jumping back to either endpoint.
  require(controller.request({42, 2, 2, 2}), "interrupting request should publish");
  controller.beginBlock();
  require(near(controller.currentValues()[0], 0.25f), "interrupt changed the rendered start");
  controller.advanceFrame();
  require(near(controller.currentValues()[0], 0.25f), "equal interrupted endpoint drifted");
  require(near(controller.currentValues()[1], std::sqrt(200.0f * 400.0f), 0.02f),
          "interrupted logarithmic transition is wrong");
  controller.advanceFrame();
  require(!controller.telemetry().transitioning, "transition did not settle");
  require(controller.telemetry().currentSceneIndex == 2, "destination did not become current");

  // Multiple unread requests collapse to the latest publication.
  require(controller.request({42, 3, 20, 1}), "first latest-wins request failed");
  require(controller.request({42, 4, 0, 3}), "second latest-wins request failed");
  controller.beginBlock();
  require(controller.telemetry().lastAppliedRequestId == 4, "latest request did not win");
  require(controller.telemetry().currentSceneIndex == 3, "instant request did not settle");

  // Commands from the engine generation that was replaced are ignored.
  require(controller.request({41, 5, 0, 1}), "stale-generation request should publish");
  controller.beginBlock();
  require(controller.telemetry().lastAppliedRequestId == 4, "stale generation was applied");
  require(controller.telemetry().currentSceneIndex == 3, "stale generation changed the scene");

  ardor::SceneTransitionController overridden;
  require(overridden.prepare(program()), "override program should prepare");
  require(overridden.request({42, 1, 4, 1}), "override transition should publish");
  overridden.beginBlock();
  overridden.advanceFrame();
  require(overridden.requestOverride(0, 0.7f), "target override should publish");
  overridden.beginBlock();
  overridden.advanceFrame();
  require(near(overridden.currentValues()[0], 0.7f),
          "target override should cancel only its active ramp");
  require(overridden.currentValues()[1] > 200.0f,
          "unrelated scene targets should continue transitioning");
  require(overridden.request({42, 2, 0, 2}), "new scene should clear overrides");
  overridden.beginBlock();
  require(near(overridden.currentValues()[0], 0.25f),
          "new scene recall should restore scene ownership");

  ardor::SceneTransitionProgram invalid = program();
  invalid.outputTrimDb[0] = 7.0f;
  require(!controller.prepare(std::move(invalid)), "invalid trim range was accepted");

  // The engine applies scene input gain before the rig and scene trim before
  // looper capture/master/limiter. Both paths use the same transition clock.
  ardor::PedalEngine engine;
  engine.prepareBlockSize(4);
  engine.setSafetyLimiterEnabled(false);
  auto engineProgram = program();
  engineProgram.outputTrimDb = {0.0f, 0.0f, -6.0206f, 0.0f};
  engineProgram.targets.resize(1);
  engineProgram.targets[0].law = ardor::SceneTransitionLaw::Decibels;
  engineProgram.targets[0].values = {0.0f, 6.0206f, 0.0f, 0.0f};
  engineProgram.targets[0].address.kind = ardor::SceneRuntimeTargetKind::InputGainDb;
  std::string error;
  require(engine.installPreparedScenes(std::move(engineProgram), error),
          "engine should install a resolved scene program");
  const float input[4] = {0.25f, 0.25f, 0.25f, 0.25f};
  float left[4]{};
  float right[4]{};
  engine.processBlock(input, left, right, 4);
  require(near(left[3], 0.25f), "default scene changed transparent engine gain");
  require(engine.tryRequestScene({42, 1, 0, 1}), "engine scene request failed");
  engine.processBlock(input, left, right, 4);
  require(near(left[3], 0.5f, 0.001f), "scene input gain was not applied before the rig");
  require(engine.tryRequestScene({42, 2, 0, 2}), "engine trim request failed");
  engine.processBlock(input, left, right, 4);
  require(near(left[3], 0.125f, 0.001f), "scene trim was not applied to preset output");
  require(engine.sceneTransitionTelemetry().currentSceneIndex == 2,
          "engine scene telemetry did not settle");
  engine.requestSceneValueSnapshot();
  engine.processBlock(input, left, right, 4);
  std::uint64_t snapshotSerial = 0;
  std::vector<float> snapshotValues;
  require(engine.tryReadSceneValueSnapshot(snapshotSerial, snapshotValues)
            && snapshotValues.size() == 1 && near(snapshotValues[0], 0.0f),
          "engine should publish settled scene values without control-thread DSP reads");
  require(!engine.tryReadSceneValueSnapshot(snapshotSerial, snapshotValues),
          "scene value snapshots should be consumed once per publication");

  ardor::PedalEngine unresolvedEngine;
  unresolvedEngine.prepareBlockSize(4);
  auto unresolved = program();
  unresolved.targets.resize(1);
  unresolved.targets[0].address.kind = ardor::SceneRuntimeTargetKind::CabParameter;
  unresolved.targets[0].address.parameterIndex = ardor::SceneRuntimeParameter::Mix;
  require(!unresolvedEngine.installPreparedScenes(std::move(unresolved), error),
          "engine accepted an unresolved numeric target");

  std::cout << "scene transition smoke passed\n";
  return 0;
}
