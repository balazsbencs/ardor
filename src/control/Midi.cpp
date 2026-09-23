#include "control/Midi.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

std::uint8_t messageDataLength(std::uint8_t status)
{
  const auto kind = static_cast<std::uint8_t>(status & 0xf0U);
  return (kind == 0xc0U || kind == 0xd0U) ? 1U : 2U;
}

const PresetSceneTarget* findSceneTarget(const PresetScene& scene,
                                         const PresetMidiAction& action)
{
  const auto wanted = action.target == PresetMidiTargetType::BlockEnabled
    ? PresetSceneTargetType::BlockEnabled : PresetSceneTargetType::Parameter;
  const auto found = std::find_if(scene.targets.begin(), scene.targets.end(),
    [&](const PresetSceneTarget& target) {
      return target.target == wanted && target.blockId == action.blockId
        && (wanted == PresetSceneTargetType::BlockEnabled
            || target.parameter == action.parameter);
    });
  return found == scene.targets.end() ? nullptr : &*found;
}

float sceneTargetValue(const PresetSceneTarget& target)
{
  return target.value.is_boolean() ? (target.value.get<bool>() ? 1.0f : 0.0f)
                                   : target.value.get<float>();
}

} // namespace

void ControllerPickup::arm(float targetPosition, float minimumMovement, float tolerance,
                           bool crossingPossible) noexcept
{
  targetPosition_ = std::clamp(targetPosition, 0.0f, 1.0f);
  minimumMovement_ = std::max(0.0f, minimumMovement);
  tolerance_ = std::max(0.0f, tolerance);
  crossingPossible_ = crossingPossible;
  originKnown_ = positionKnown_;
  if (originKnown_) originPosition_ = lastPosition_;
  freshMovement_ = false;
  pending_ = true;
}

void ControllerPickup::reset() noexcept
{
  positionKnown_ = false;
  originKnown_ = false;
  freshMovement_ = false;
  pending_ = false;
}

bool ControllerPickup::observe(float position) noexcept
{
  position = std::clamp(position, 0.0f, 1.0f);
  if (!positionKnown_) {
    lastPosition_ = position;
    positionKnown_ = true;
    if (pending_ && !originKnown_) {
      originPosition_ = position;
      originKnown_ = true;
      return false;
    }
    return !pending_;
  }
  const float previous = lastPosition_;
  lastPosition_ = position;
  if (!pending_) return true;
  if (!originKnown_) {
    originPosition_ = position;
    originKnown_ = true;
    return false;
  }
  freshMovement_ = freshMovement_
    || std::fabs(position - originPosition_) >= minimumMovement_;
  if (!freshMovement_) return false;
  const bool close = std::fabs(position - targetPosition_) <= tolerance_;
  const bool crossed = crossingPossible_
    && ((previous <= targetPosition_ && position >= targetPosition_)
        || (previous >= targetPosition_ && position <= targetPosition_));
  if (!close && !crossed && crossingPossible_) return false;
  pending_ = false;
  return true;
}

std::optional<MidiMessage> MidiStreamParser::push(std::uint8_t byte)
{
  if (byte >= 0xf8U) {
    return std::nullopt;
  }

  if ((byte & 0x80U) != 0U) {
    dataCount_ = 0;
    if (byte >= 0xf0U) {
      status_ = 0;
      return std::nullopt;
    }
    status_ = byte;
    return std::nullopt;
  }

  if (status_ == 0) {
    return std::nullopt;
  }

  data_[dataCount_++] = byte;
  if (dataCount_ < messageDataLength(status_)) {
    return std::nullopt;
  }
  dataCount_ = 0;

  const auto kind = static_cast<std::uint8_t>(status_ & 0xf0U);
  const auto channel = static_cast<std::uint8_t>(status_ & 0x0fU);
  if (kind == 0xc0U) {
    return MidiMessage{MidiMessageType::ProgramChange, channel, data_[0], 0};
  }
  if (kind == 0xb0U) {
    return MidiMessage{MidiMessageType::ControlChange, channel, data_[0], data_[1]};
  }
  return std::nullopt;
}

void MidiStreamParser::reset()
{
  status_ = 0;
  dataCount_ = 0;
}

MidiControlMapper::MidiControlMapper(MidiControlMapping mapping)
  : mapping_(mapping)
{
}

std::optional<MidiAction> MidiControlMapper::map(const MidiMessage& message) const
{
  if (mapping_.channel >= 0 && message.channel != mapping_.channel) {
    return std::nullopt;
  }

  if (message.type == MidiMessageType::ProgramChange) {
    if (message.data1 < 4) {
      return MidiAction{MidiActionType::SelectPreset, message.data1};
    }
    return std::nullopt;
  }

  if ((message.data1 == 0 || message.data1 == 32) && message.data2 < 100) {
    return MidiAction{MidiActionType::SelectBank, message.data2};
  }
  if (message.data1 == mapping_.tunerControlChange) {
    return MidiAction{MidiActionType::SetTuner, message.data2 >= 64 ? 1 : 0};
  }
  return std::nullopt;
}

void PresetMidiMapper::load(const std::vector<PresetMidiBinding>& bindings)
{
  bindings_.clear();
  bindings_.reserve(bindings.size());
  for (const auto& binding : bindings) {
    bindings_.push_back({binding, false,
                         std::vector<bool>(binding.actions.size(), false),
                         std::vector<ControllerPickup>(binding.actions.size())});
  }
}

std::vector<PresetMidiValue> PresetMidiMapper::reset()
{
  std::vector<PresetMidiValue> values;
  for (auto& state : bindings_) {
    state.inputHigh = false;
    std::fill(state.scene2.begin(), state.scene2.end(), false);
    for (auto& pickup : state.pickup) pickup.reset();
    if (state.binding.mode != PresetMidiBindingMode::Toggle) continue;
    for (const auto& action : state.binding.actions) {
      values.push_back({action, action.value1});
    }
  }
  return values;
}

void PresetMidiMapper::rearmSceneOwned(const PresetScene& destination)
{
  constexpr float kMidiStep = 1.0f / 127.0f;
  for (auto& state : bindings_) {
    for (std::size_t index = 0; index < state.binding.actions.size(); ++index) {
      const auto& action = state.binding.actions[index];
      const auto* target = findSceneTarget(destination, action);
      if (!target) continue;
      if (state.binding.mode == PresetMidiBindingMode::Toggle) {
        state.scene2[index] = false;
        continue;
      }
      const float span = action.value2 - action.value1;
      const bool crossingPossible = std::fabs(span) > 1.0e-9f;
      const float targetPosition = crossingPossible
        ? (sceneTargetValue(*target) - action.value1) / span : 0.0f;
      state.pickup[index].arm(targetPosition, 2.0f * kMidiStep,
                              2.0f * kMidiStep, crossingPossible);
    }
  }
}

bool PresetMidiMapper::handles(const MidiMessage& message) const
{
  if (message.type != MidiMessageType::ControlChange) return false;
  for (const auto& state : bindings_) {
    if (state.binding.controlChange == message.data1
        && (state.binding.channel < 0 || state.binding.channel == message.channel)) return true;
  }
  return false;
}

std::vector<PresetMidiValue> PresetMidiMapper::map(const MidiMessage& message)
{
  std::vector<PresetMidiValue> values;
  if (message.type != MidiMessageType::ControlChange) return values;
  for (auto& state : bindings_) {
    const auto& binding = state.binding;
    if (binding.controlChange != message.data1
        || (binding.channel >= 0 && binding.channel != message.channel)) continue;
    if (binding.mode == PresetMidiBindingMode::Continuous) {
      const float position = static_cast<float>(message.data2) / 127.0f;
      for (std::size_t index = 0; index < binding.actions.size(); ++index) {
        const auto& action = binding.actions[index];
        if (!state.pickup[index].observe(position)) continue;
        values.push_back({action, midiActionValueAt(action, message.data2)});
      }
      continue;
    }

    const bool high = message.data2 >= 64;
    if (high && !state.inputHigh) {
      for (std::size_t index = 0; index < binding.actions.size(); ++index) {
        const auto& action = binding.actions[index];
        state.scene2[index] = !state.scene2[index];
        values.push_back({action, state.scene2[index] ? action.value2 : action.value1});
      }
    }
    state.inputHigh = high;
  }
  return values;
}

void SceneMidiMapper::load(const std::vector<PresetSceneMidiBinding>& bindings,
                           const std::optional<PresetSceneSet>& sceneSet)
{
  bindings_.clear();
  bindings_.reserve(bindings.size());
  for (const auto& binding : bindings) {
    int sceneIndex = -1;
    if (binding.action == PresetSceneMidiActionType::SelectScene && sceneSet) {
      const auto found = std::find_if(
        sceneSet->scenes.begin(), sceneSet->scenes.end(),
        [&](const PresetScene& scene) { return scene.id == binding.sceneId; });
      if (found != sceneSet->scenes.end()) {
        sceneIndex = static_cast<int>(std::distance(sceneSet->scenes.begin(), found));
      }
    }
    bindings_.push_back({binding, sceneIndex, false, -1});
  }
}

void SceneMidiMapper::resetInputState()
{
  for (auto& state : bindings_) {
    state.inputHigh = false;
    state.lastSceneNumber = -1;
  }
}

bool SceneMidiMapper::handles(const MidiMessage& message) const
{
  if (message.type != MidiMessageType::ControlChange) return false;
  return std::any_of(bindings_.begin(), bindings_.end(), [&](const BindingState& state) {
    return state.binding.controlChange == message.data1
      && (state.binding.channel < 0 || state.binding.channel == message.channel);
  });
}

std::optional<SceneMidiAction> SceneMidiMapper::map(const MidiMessage& message)
{
  if (message.type != MidiMessageType::ControlChange) return std::nullopt;
  for (auto& state : bindings_) {
    const auto& binding = state.binding;
    if (binding.controlChange != message.data1
        || (binding.channel >= 0 && binding.channel != message.channel)) continue;
    if (binding.action == PresetSceneMidiActionType::SceneNumber) {
      if (message.data2 <= 3 && state.lastSceneNumber != message.data2) {
        state.lastSceneNumber = message.data2;
        return SceneMidiAction{SceneMidiActionType::SelectScene, message.data2};
      }
      if (message.data2 > 3) state.lastSceneNumber = -1;
      return std::nullopt;
    }
    const bool high = message.data2 >= 64;
    const bool rising = high && !state.inputHigh;
    state.inputHigh = high;
    if (!rising) return std::nullopt;
    switch (binding.action) {
      case PresetSceneMidiActionType::SelectScene:
        if (state.sceneIndex >= 0) {
          return SceneMidiAction{SceneMidiActionType::SelectScene, state.sceneIndex};
        }
        return std::nullopt;
      case PresetSceneMidiActionType::ShowPresets:
        return SceneMidiAction{SceneMidiActionType::ShowPresets, 0};
      case PresetSceneMidiActionType::ShowScenes:
        return SceneMidiAction{SceneMidiActionType::ShowScenes, 0};
      case PresetSceneMidiActionType::SceneNumber:
        break;
    }
  }
  return std::nullopt;
}

} // namespace ardor
