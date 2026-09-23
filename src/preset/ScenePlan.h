#pragma once

#include "dsp/SceneTransition.h"
#include "preset/Preset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ardor {

enum class SceneBypassCapability {
  NotApplicable,
  CrossfadeCut,
  TailCapable,
};

struct SceneBlockLocation {
  SceneBlockContainer container = SceneBlockContainer::None;
  std::uint16_t topIndex = 0;
  std::uint16_t childIndex = 0;
  bool child = false;
};

struct ScenePreparedTarget {
  SceneRuntimeTargetKind kind = SceneRuntimeTargetKind::InputGainDb;
  SceneTransitionLaw transition = SceneTransitionLaw::Linear;
  SceneBypassCapability bypass = SceneBypassCapability::NotApplicable;
  SceneBlockLocation location;
  // The meaning of parameterIndex is scoped by kind. Daisy parameters use
  // their immutable descriptor index; fixed processors use their setter-table
  // index. This avoids string dispatch in the future realtime scene runner.
  std::uint16_t parameterIndex = 0;
  SceneWdwLane lane = SceneWdwLane::None;
  std::array<float, 4> values{};
};

struct ScenePreparedDefinition {
  std::string id;
  std::string name;
  std::uint32_t enterTimeMs = 0;
  float outputTrimDb = 0.0f;
};

struct SceneAdmissionAccounting {
  // Processor/state instances retained by the prepared rig, including blocks
  // that begin disabled but are addressable by scenes or legacy MIDI.
  std::size_t preparedProcessors = 0;
  // Greatest steady-state render count among the four authored scenes.
  std::size_t maximumSteadyStateProcessors = 0;
  // Greatest render count across every source/destination pair. Let-ring
  // effects are charged if any earlier scene can have left a tail alive.
  std::size_t maximumTransitionProcessors = 0;
  // Greatest number of qualified tails that can remain after entering one
  // destination scene, including tails accumulated through repeated recalls.
  std::size_t maximumRetainedTails = 0;
};

struct ScenePlan {
  std::array<ScenePreparedDefinition, 4> scenes;
  std::vector<ScenePreparedTarget> targets;
  std::size_t defaultSceneIndex = 0;
  SceneAdmissionAccounting admission;
};

// Resolves scene addresses and validates that every target can be updated by
// the already-prepared DSP program. The result contains bounded numeric paths
// and scalar values, so later runtime activation does not inspect preset JSON.
bool buildScenePlan(const Preset& preset, ScenePlan& plan, std::string& error);
// Applies platform limits to the accounting produced by buildScenePlan().
// Processor units are deliberately conservative: each prepared block counts
// once regardless of its implementation cost. Phase-8 measured CPU gates can
// tighten the concurrent limit without changing the scene document contract.
bool admitScenePlan(const ScenePlan& plan, std::size_t preparedProcessorLimit,
                    std::size_t concurrentProcessorLimit, std::string& error);
// Captures every currently defined value that the realtime scene runner can
// safely own. The returned address order is stable and can be copied into all
// four scenes when scene authoring is enabled.
std::vector<PresetSceneTarget> captureSceneTargets(const Preset& preset);
SceneTransitionProgram makeSceneTransitionProgram(const ScenePlan& plan,
                                                  std::uint64_t presetGeneration);

} // namespace ardor
