#include "preset/ScenePlan.h"

#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ardor {
namespace {

struct ResolvedBlock {
  const PresetBlock* block = nullptr;
  SceneBlockLocation location;
};

struct ParameterCapability {
  SceneRuntimeTargetKind kind;
  SceneTransitionLaw transition;
  float minimum;
  float maximum;
  std::uint16_t parameterIndex;
};

std::unordered_set<std::string> retainedBlockIds(const Preset& preset)
{
  std::unordered_set<std::string> result;
  for (const auto& binding : preset.midiBindings) {
    for (const auto& action : binding.actions) {
      if (action.target == PresetMidiTargetType::BlockEnabled) result.insert(action.blockId);
    }
  }
  if (preset.sceneSet) {
    for (const auto& scene : preset.sceneSet->scenes) {
      for (const auto& target : scene.targets) {
        if (target.target == PresetSceneTargetType::Parameter
            || target.target == PresetSceneTargetType::BlockEnabled)
          result.insert(target.blockId);
      }
    }
  }
  return result;
}

std::optional<ResolvedBlock> findInBlocks(const std::vector<PresetBlock>& blocks,
                                           std::string_view id,
                                           SceneBlockContainer container,
                                           const std::unordered_set<std::string>& retained)
{
  std::uint16_t runtimeTop = 0;
  for (std::size_t top = 0; top < blocks.size(); ++top) {
    const bool topPrepared = blocks[top].enabled || retained.contains(blocks[top].id);
    if (blocks[top].id == id) {
      return ResolvedBlock{&blocks[top], {container, runtimeTop, 0, false}};
    }
    if (blocks[top].type != "dualRig") {
      if (topPrepared) ++runtimeTop;
      continue;
    }
    for (std::size_t lane = 0; lane < blocks[top].lanes.size(); ++lane) {
      std::uint16_t runtimeChild = 0;
      for (std::size_t child = 0; child < blocks[top].lanes[lane].size(); ++child) {
        if (blocks[top].lanes[lane][child].id == id) {
          if (!topPrepared) return std::nullopt;
          return ResolvedBlock{
            &blocks[top].lanes[lane][child],
            {lane == 0 ? SceneBlockContainer::DualRigLeft : SceneBlockContainer::DualRigRight,
             runtimeTop, runtimeChild, true}};
        }
        if (blocks[top].lanes[lane][child].enabled
            || retained.contains(blocks[top].lanes[lane][child].id)) ++runtimeChild;
      }
    }
    if (topPrepared) ++runtimeTop;
  }
  return std::nullopt;
}

std::optional<ResolvedBlock> findBlock(const Preset& preset, std::string_view id)
{
  const auto retained = retainedBlockIds(preset);
  if (auto block = findInBlocks(preset.blocks, id, SceneBlockContainer::Serial, retained)) return block;
  if (!preset.wdw) return std::nullopt;
  if (auto block = findInBlocks(preset.wdw->dry.blocks, id, SceneBlockContainer::WdwDry, retained)) return block;
  return findInBlocks(preset.wdw->wet.blocks, id, SceneBlockContainer::WdwWet, retained);
}

std::optional<ParameterCapability> fixedCapability(const PresetBlock& block,
                                                    std::string_view key)
{
  const auto linear = SceneTransitionLaw::Linear;
  const auto db = SceneTransitionLaw::Decibels;
  const auto log = SceneTransitionLaw::LogFrequency;
  if (block.type == "cab") {
    if (key == "mix") return ParameterCapability{SceneRuntimeTargetKind::CabParameter, linear, 0, 1, Mix};
    if (key == "levelDb") return ParameterCapability{SceneRuntimeTargetKind::CabParameter, db, -60, 12, LevelDb};
  } else if (block.type == "irreverb") {
    if (key == "mix") return ParameterCapability{SceneRuntimeTargetKind::IrReverbParameter, linear, 0, 1, Mix};
    if (key == "levelDb") return ParameterCapability{SceneRuntimeTargetKind::IrReverbParameter, db, -60, 12, LevelDb};
    if (key == "preDelayMs") return ParameterCapability{SceneRuntimeTargetKind::IrReverbParameter, linear, 0, 500, PreDelayMs};
    if (key == "lowCutHz") return ParameterCapability{SceneRuntimeTargetKind::IrReverbParameter, log, 20, 2000, LowCutHz};
    if (key == "highCutHz") return ParameterCapability{SceneRuntimeTargetKind::IrReverbParameter, log, 500, 20000, HighCutHz};
  } else if (block.type == "stereo") {
    if (key == "width") return ParameterCapability{SceneRuntimeTargetKind::StereoParameter, linear, 0, 2, Width};
    if (key == "delayMs") return ParameterCapability{SceneRuntimeTargetKind::StereoParameter, linear, 0, 30, DelayMs};
    if (key == "bassMonoHz") return ParameterCapability{SceneRuntimeTargetKind::StereoParameter, log, 0, 500, BassMonoHz};
    if (key == "levelDb") return ParameterCapability{SceneRuntimeTargetKind::StereoParameter, db, -24, 12, LevelDb};
  } else if (block.type == "wah" && block.params.value("mode", std::string{"gcb95"}) == "gcb95") {
    if (key == "position") return ParameterCapability{SceneRuntimeTargetKind::WahParameter, linear, 0, 1, Position};
    if (key == "level") return ParameterCapability{SceneRuntimeTargetKind::WahParameter, db, -24, 24, LevelDb};
  } else if (block.type == "dynamics") {
    const auto mode = block.params.value("mode", "");
    if (mode == "compressor") {
      const auto kind = SceneRuntimeTargetKind::CompressorParameter;
      if (key == "threshold_db") return ParameterCapability{kind, db, -60, 0, ThresholdDb};
      if (key == "ratio") return ParameterCapability{kind, linear, 1, 20, Ratio};
      if (key == "attack_ms") return ParameterCapability{kind, log, 0.1f, 200, AttackMs};
      if (key == "release_ms") return ParameterCapability{kind, log, 10, 2000, ReleaseMs};
      if (key == "knee_db") return ParameterCapability{kind, db, 0, 24, KneeDb};
      if (key == "makeup_db") return ParameterCapability{kind, db, 0, 24, MakeupDb};
      if (key == "input_gain_db") return ParameterCapability{kind, db, -24, 24, InputGainDb};
      if (key == "mix") return ParameterCapability{kind, linear, 0, 1, Mix};
      if (key == "sidechain_hpf_hz") return ParameterCapability{kind, log, 20, 500, SidechainHpfHz};
    } else if (mode == "noise_gate") {
      const auto kind = SceneRuntimeTargetKind::NoiseGateParameter;
      if (key == "threshold_db") return ParameterCapability{kind, db, -80, 0, ThresholdDb};
      if (key == "reduction_db") return ParameterCapability{kind, db, 0, 96, ReductionDb};
      if (key == "attack_ms") return ParameterCapability{kind, log, 0.1f, 50, AttackMs};
      if (key == "hold_ms") return ParameterCapability{kind, linear, 0, 500, HoldMs};
      if (key == "release_ms") return ParameterCapability{kind, log, 10, 2000, ReleaseMs};
      if (key == "hysteresis_db") return ParameterCapability{kind, db, 0, 18, HysteresisDb};
      if (key == "sidechain_hpf_hz") return ParameterCapability{kind, log, 20, 500, SidechainHpfHz};
    } else if (mode == "transient_shaper") {
      const auto kind = SceneRuntimeTargetKind::TransientShaperParameter;
      if (key == "attack") return ParameterCapability{kind, linear, -100, 100, Attack};
      if (key == "sustain") return ParameterCapability{kind, linear, -100, 100, Sustain};
      if (key == "mix") return ParameterCapability{kind, linear, 0, 1, Mix};
      if (key == "output_db") return ParameterCapability{kind, db, -24, 24, OutputDb};
    }
  } else if (block.type == "distortion") {
    const auto mode = block.params.value("mode", std::string{"rat"});
    const auto kind = SceneRuntimeTargetKind::DistortionParameter;
    if (mode == "rat" && (key == "distortion" || key == "filter" || key == "volume"))
      return ParameterCapability{kind, linear, 0, 1,
        key == "distortion" ? Distortion : key == "filter" ? Filter : Volume};
    if (mode == "big_cheese" && (key == "fuzz" || key == "tone" || key == "volume"))
      return ParameterCapability{kind, linear, 0, 1,
        key == "fuzz" ? Fuzz : key == "tone" ? Tone : Volume};
    if (mode == "tape") {
      if (key == "drive") return ParameterCapability{kind, db, -12, 24, Drive};
      if (key == "output_db") return ParameterCapability{kind, db, -24, 24, OutputDb};
      if (key == "hiss_db") return ParameterCapability{kind, db, -120, -60, HissDb};
      if (key == "mix") return ParameterCapability{kind, linear, 0, 1, Mix};
      if (key == "flutter") return ParameterCapability{kind, linear, 0, 1, Flutter};
      if (key == "saturation") return ParameterCapability{kind, linear, 0, 1, Saturation};
      if (key == "bias") return ParameterCapability{kind, linear, 0, 1, Bias};
      if (key == "head_bump") return ParameterCapability{kind, linear, 0, 1, HeadBump};
    }
  }
  return std::nullopt;
}

std::optional<ParameterCapability> capabilityFor(const PresetBlock& block,
                                                  std::string_view key)
{
  if (block.type == "mod" || block.type == "delay" || block.type == "reverb") {
    // Delay time changes can alter buffer/read-head behavior and stay shared
    // until that processor has an explicit click-free scene transition.
    if (block.type == "delay" && key == "time") return std::nullopt;
    const auto* effect = findDaisyFxDescriptor(block.type, block.params.value("mode", ""));
    if (!effect) return std::nullopt;
    const auto parameter = std::find_if(effect->params.begin(), effect->params.end(),
      [&](const DaisyFxParamDescriptor& item) { return item.key == key; });
    if (parameter == effect->params.end()) return std::nullopt;
    const auto controls = daisyFxParamControlSpec(*effect, *parameter);
    return ParameterCapability{SceneRuntimeTargetKind::DaisyParameter,
      controls.choiceValues.empty() ? SceneTransitionLaw::Linear : SceneTransitionLaw::Stepped,
      0, 1, static_cast<std::uint16_t>(std::distance(effect->params.begin(), parameter))};
  }
  return fixedCapability(block, key);
}

std::optional<SceneBypassCapability> bypassCapabilityFor(const PresetBlock& block)
{
  if (block.type == "delay" || block.type == "reverb" || block.type == "irreverb")
    return SceneBypassCapability::TailCapable;
  if (block.type == "mod" || block.type == "stereo" || block.type == "dynamics"
      || block.type == "distortion" || block.type == "wah" || block.type == "eq")
    return SceneBypassCapability::CrossfadeCut;
  return std::nullopt;
}

using SceneEnabledStates = std::array<bool, 4>;

struct AdmissionBlock {
  SceneEnabledStates enabled{};
  bool letRing = false;
};

std::unordered_set<std::string> legacyEnabledBlockIds(const Preset& preset)
{
  std::unordered_set<std::string> result;
  for (const auto& binding : preset.midiBindings) {
    for (const auto& action : binding.actions) {
      if (action.target == PresetMidiTargetType::BlockEnabled) result.insert(action.blockId);
    }
  }
  return result;
}

void collectAdmissionBlocks(const std::vector<PresetBlock>& blocks,
                            const std::unordered_set<std::string>& retained,
                            const std::unordered_set<std::string>& legacyEnabled,
                            const std::unordered_map<std::string, SceneEnabledStates>& sceneEnabled,
                            bool parentPrepared, std::vector<AdmissionBlock>& result)
{
  for (const auto& block : blocks) {
    const bool prepared = parentPrepared && (block.enabled || retained.contains(block.id));
    if (!prepared) continue;
    AdmissionBlock admitted;
    admitted.enabled.fill(block.enabled);
    if (const auto values = sceneEnabled.find(block.id); values != sceneEnabled.end()) {
      admitted.enabled = values->second;
    }
    // Legacy block-enable controllers may be moved at any time, independent
    // of scene selection, so admission reserves their active render cost.
    if (legacyEnabled.contains(block.id)) admitted.enabled.fill(true);
    admitted.letRing = block.sceneBypass == PresetSceneBypassPolicy::LetRing
      && bypassCapabilityFor(block) == SceneBypassCapability::TailCapable;
    result.push_back(admitted);
    for (const auto& lane : block.lanes) {
      collectAdmissionBlocks(lane, retained, legacyEnabled, sceneEnabled,
                             prepared, result);
    }
  }
}

SceneAdmissionAccounting sceneAdmissionAccounting(const Preset& preset)
{
  std::unordered_map<std::string, SceneEnabledStates> sceneEnabled;
  if (preset.sceneSet) {
    for (std::size_t scene = 0; scene < preset.sceneSet->scenes.size(); ++scene) {
      for (const auto& target : preset.sceneSet->scenes[scene].targets) {
        if (target.target != PresetSceneTargetType::BlockEnabled
            || !target.value.is_boolean()) continue;
        auto [entry, inserted] = sceneEnabled.try_emplace(target.blockId);
        if (inserted) entry->second.fill(false);
        entry->second[scene] = target.value.get<bool>();
      }
    }
  }

  const auto retained = retainedBlockIds(preset);
  const auto legacyEnabled = legacyEnabledBlockIds(preset);
  std::vector<AdmissionBlock> blocks;
  if (preset.wdw) {
    collectAdmissionBlocks(preset.wdw->dry.blocks, retained, legacyEnabled,
                           sceneEnabled, true, blocks);
    collectAdmissionBlocks(preset.wdw->wet.blocks, retained, legacyEnabled,
                           sceneEnabled, true, blocks);
  } else {
    collectAdmissionBlocks(preset.blocks, retained, legacyEnabled,
                           sceneEnabled, true, blocks);
  }

  SceneAdmissionAccounting accounting;
  accounting.preparedProcessors = blocks.size();
  for (std::size_t destination = 0; destination < 4; ++destination) {
    std::size_t steady = 0;
    std::size_t retainedTails = 0;
    for (const auto& block : blocks) {
      const bool everEnabled = std::any_of(block.enabled.begin(), block.enabled.end(),
                                           [](bool value) { return value; });
      if (block.enabled[destination]) ++steady;
      if (block.letRing && everEnabled && !block.enabled[destination]) ++retainedTails;
    }
    accounting.maximumSteadyStateProcessors = std::max(
      accounting.maximumSteadyStateProcessors, steady);
    accounting.maximumRetainedTails = std::max(
      accounting.maximumRetainedTails, retainedTails);

    for (std::size_t source = 0; source < 4; ++source) {
      std::size_t active = 0;
      for (const auto& block : blocks) {
        const bool everEnabled = std::any_of(block.enabled.begin(), block.enabled.end(),
                                             [](bool value) { return value; });
        // Cut blocks overlap only the immediate source during their safety
        // fade. Let-ring blocks can retain history from any earlier scene.
        if (block.enabled[destination]
            || (block.letRing && everEnabled)
            || (!block.letRing && block.enabled[source])) ++active;
      }
      accounting.maximumTransitionProcessors = std::max(
        accounting.maximumTransitionProcessors, active);
    }
  }
  return accounting;
}

bool numberValue(const nlohmann::json& value, float& result)
{
  if (!value.is_number()) return false;
  result = value.get<float>();
  return std::isfinite(result);
}

bool prepareTarget(const Preset& preset, const PresetSceneTarget& source,
                   ScenePreparedTarget& target, float& value, std::string& error)
{
  if (source.target == PresetSceneTargetType::InputGainDb) {
    if (!numberValue(source.value, value) || value < -60 || value > 24) {
      error = "scene inputGainDb must be between -60 and 24";
      return false;
    }
    target.kind = SceneRuntimeTargetKind::InputGainDb;
    target.transition = SceneTransitionLaw::Decibels;
    return true;
  }
  if (source.target == PresetSceneTargetType::WdwLane) {
    if (!preset.wdw) {
      error = "scene wet/dry/wet lane target requires wet/dry/wet routing";
      return false;
    }
    target.kind = SceneRuntimeTargetKind::WdwLaneParameter;
    target.lane = source.lane == "dry" ? SceneWdwLane::Dry : SceneWdwLane::Wet;
    if (source.parameter == "enabled") {
      if (!source.value.is_boolean()) { error = "scene lane enabled value must be boolean"; return false; }
      value = source.value.get<bool>() ? 1.0f : 0.0f;
      target.transition = SceneTransitionLaw::Stepped;
      target.parameterIndex = Enabled;
      return true;
    }
    if (!numberValue(source.value, value)) { error = "scene lane value must be finite"; return false; }
    if (source.parameter == "levelDb" && value >= -60 && value <= 12) {
      target.transition = SceneTransitionLaw::Decibels;
      target.parameterIndex = LevelDb;
    } else if (source.lane == "dry" && source.parameter == "pan" && value >= -1 && value <= 1) {
      target.transition = SceneTransitionLaw::Linear;
      target.parameterIndex = Pan;
    } else if (source.lane == "wet" && source.parameter == "width" && value >= 0 && value <= 1) {
      target.transition = SceneTransitionLaw::Linear;
      target.parameterIndex = Width;
    } else { error = "scene lane parameter is unsupported or out of range"; return false; }
    return true;
  }

  const auto resolved = findBlock(preset, source.blockId);
  if (!resolved) { error = "scene target block was not found: " + source.blockId; return false; }
  target.location = resolved->location;
  if (source.target == PresetSceneTargetType::BlockEnabled) {
    const auto bypass = bypassCapabilityFor(*resolved->block);
    if (!bypass) {
      error = "block type is shared-only and cannot be bypassed by scenes: " + source.blockId;
      return false;
    }
    if (!source.value.is_boolean()) { error = "scene block enabled value must be boolean"; return false; }
    value = source.value.get<bool>() ? 1.0f : 0.0f;
    target.kind = SceneRuntimeTargetKind::BlockEnabled;
    target.transition = SceneTransitionLaw::Stepped;
    target.bypass = *bypass;
    return true;
  }

  const auto capability = capabilityFor(*resolved->block, source.parameter);
  if (!capability) {
    error = "parameter is not scene-capable: " + source.blockId + "/" + source.parameter;
    return false;
  }
  if (!numberValue(source.value, value)
      || value < capability->minimum || value > capability->maximum) {
    error = "scene parameter is out of range: " + source.blockId + "/" + source.parameter;
    return false;
  }
  target.kind = capability->kind;
  target.transition = capability->transition;
  target.parameterIndex = capability->parameterIndex;
  return true;
}

} // namespace

std::vector<PresetSceneTarget> captureSceneTargets(const Preset& preset)
{
  std::vector<PresetSceneTarget> targets;
  targets.push_back({PresetSceneTargetType::InputGainDb, {}, {}, {}, preset.global.inputGainDb});

  const auto appendBlocks = [&](const auto& self, const std::vector<PresetBlock>& blocks) -> void {
    for (const auto& block : blocks) {
      if (bypassCapabilityFor(block)) {
        targets.push_back({PresetSceneTargetType::BlockEnabled, block.id, {}, {}, block.enabled});
      }
      if (block.params.is_object()) {
        for (auto item = block.params.begin(); item != block.params.end(); ++item) {
          const auto capability = capabilityFor(block, item.key());
          float value = 0.0f;
          if (!capability || !numberValue(item.value(), value)
              || value < capability->minimum || value > capability->maximum) continue;
          targets.push_back({PresetSceneTargetType::Parameter, block.id, item.key(), {}, item.value()});
        }
      }
      for (const auto& lane : block.lanes) self(self, lane);
    }
  };

  if (preset.wdw) {
    const auto appendLane = [&](const WdwLane& lane, const char* name, bool dry) {
      targets.push_back({PresetSceneTargetType::WdwLane, {}, "enabled", name, lane.enabled});
      targets.push_back({PresetSceneTargetType::WdwLane, {}, "levelDb", name, lane.levelDb});
      targets.push_back({PresetSceneTargetType::WdwLane, {}, dry ? "pan" : "width", name,
                         dry ? lane.pan : lane.width});
      appendBlocks(appendBlocks, lane.blocks);
    };
    appendLane(preset.wdw->dry, "dry", true);
    appendLane(preset.wdw->wet, "wet", false);
  } else {
    appendBlocks(appendBlocks, preset.blocks);
  }
  return targets;
}

bool buildScenePlan(const Preset& preset, ScenePlan& plan, std::string& error)
{
  plan = {};
  if (!preset.sceneSet) {
    error = "preset has no scene set";
    return false;
  }
  const auto& set = *preset.sceneSet;
  bool foundDefault = false;
  const auto targetCount = set.scenes[0].targets.size();
  for (std::size_t scene = 0; scene < set.scenes.size(); ++scene) {
    const auto& source = set.scenes[scene];
    if (source.targets.size() != targetCount) {
      error = "scene target addresses must match in all four scenes";
      return false;
    }
    plan.scenes[scene] = {source.id, source.name, source.enterTimeMs, source.outputTrimDb};
    if (source.id == set.defaultSceneId) {
      plan.defaultSceneIndex = scene;
      foundDefault = true;
    }
  }
  if (!foundDefault) {
    error = "default scene id does not identify one of the four scenes";
    return false;
  }
  plan.targets.resize(targetCount);
  for (std::size_t index = 0; index < plan.targets.size(); ++index) {
    for (std::size_t scene = 0; scene < set.scenes.size(); ++scene) {
      ScenePreparedTarget candidate;
      float value = 0.0f;
      if (!prepareTarget(preset, set.scenes[scene].targets[index], candidate, value, error)) {
        error = "scene " + set.scenes[scene].id + ": " + error;
        plan = {};
        return false;
      }
      if (scene == 0) {
        plan.targets[index] = std::move(candidate);
      } else if (candidate.kind != plan.targets[index].kind
                 || candidate.location.container != plan.targets[index].location.container
                 || candidate.location.topIndex != plan.targets[index].location.topIndex
                 || candidate.location.childIndex != plan.targets[index].location.childIndex
                 || candidate.location.child != plan.targets[index].location.child
                 || candidate.parameterIndex != plan.targets[index].parameterIndex
                 || candidate.bypass != plan.targets[index].bypass
                 || candidate.lane != plan.targets[index].lane) {
        error = "scene target addresses must match in all four scenes";
        plan = {};
        return false;
      }
      plan.targets[index].values[scene] = value;
    }
  }
  plan.admission = sceneAdmissionAccounting(preset);
  error.clear();
  return true;
}

bool admitScenePlan(const ScenePlan& plan, std::size_t preparedProcessorLimit,
                    std::size_t concurrentProcessorLimit, std::string& error)
{
  if (preparedProcessorLimit > 0
      && plan.admission.preparedProcessors > preparedProcessorLimit) {
    error = "scene preparation requires "
      + std::to_string(plan.admission.preparedProcessors)
      + " processor states; the platform limit is "
      + std::to_string(preparedProcessorLimit);
    return false;
  }
  if (concurrentProcessorLimit > 0
      && plan.admission.maximumTransitionProcessors > concurrentProcessorLimit) {
    error = "scene transitions may render "
      + std::to_string(plan.admission.maximumTransitionProcessors)
      + " processors concurrently, including up to "
      + std::to_string(plan.admission.maximumRetainedTails)
      + " retained tails; the platform limit is "
      + std::to_string(concurrentProcessorLimit);
    return false;
  }
  error.clear();
  return true;
}

SceneTransitionProgram makeSceneTransitionProgram(const ScenePlan& plan,
                                                  std::uint64_t presetGeneration)
{
  SceneTransitionProgram program;
  program.presetGeneration = presetGeneration;
  program.defaultSceneIndex = static_cast<std::uint8_t>(plan.defaultSceneIndex);
  program.targets.reserve(plan.targets.size());
  for (const auto& target : plan.targets) {
    SceneRuntimeAddress address;
    address.kind = target.kind;
    address.container = target.location.container;
    address.topIndex = target.location.topIndex;
    address.childIndex = target.location.childIndex;
    address.parameterIndex = target.parameterIndex;
    address.lane = target.lane;
    address.child = target.location.child;
    program.targets.push_back({target.transition, target.values, address});
  }
  for (std::size_t scene = 0; scene < plan.scenes.size(); ++scene) {
    program.outputTrimDb[scene] = plan.scenes[scene].outputTrimDb;
  }
  return program;
}

} // namespace ardor
