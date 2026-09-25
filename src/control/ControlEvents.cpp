#include "control/ControlEvents.h"

#include <algorithm>

namespace ardor {

bool applyControlEvent(ControlState& state, const ControlEvent& event)
{
  if (event.type == ControlEventType::FootswitchPressed) {
    if (event.index < 0 || event.index >= 4) {
      return false;
    }
    state.activeSlot = event.index;
    return true;
  }

  if (event.type == ControlEventType::FootswitchReleased) {
    return event.index >= 0 && event.index < 4;
  }

  state.masterVolume = std::clamp(state.masterVolume + event.delta, 0, 100);
  return true;
}

std::optional<FootswitchAction> FootswitchGesture::handle(const ControlEvent& event,
                                                          Clock::time_point now)
{
  if (event.type != ControlEventType::FootswitchPressed
      && event.type != ControlEventType::FootswitchReleased) {
    return std::nullopt;
  }
  if (event.index < 0 || event.index >= static_cast<int>(down_.size())) {
    return std::nullopt;
  }

  const auto index = static_cast<std::size_t>(event.index);
  const bool enhanced = scenesAvailable_ && layerChordEnabled_;
  const auto selectAction = [this](int selected) {
    return FootswitchAction{
      sceneLayer_ ? FootswitchActionType::SelectScene : FootswitchActionType::SelectPreset,
      selected,
    };
  };
  if (event.type == ControlEventType::FootswitchPressed) {
    if (down_[index]) {
      return std::nullopt;
    }
    down_[index] = true;
    const bool looperCandidate = !sceneLayer_
      && static_cast<int>(index) == looperEntrySlot_;
    if (!enhanced && index >= 2 && !looperCandidate) {
      return selectAction(event.index);
    }

    pressedAt_[index] = now;
    pending_[index] = true;
    if (enhanced && std::count(down_.begin(), down_.end(), true) >= 3) {
      pending_.fill(false);
      activePair_ = -1;
      pairTriggered_ = false;
      cancelledUntilRelease_ = true;
      return std::nullopt;
    }
    if (cancelledUntilRelease_) return std::nullopt;
    const auto other = index ^ 1U;
    const auto window = enhanced ? sceneChordWindow : chordWindow;
    if (down_[other]
        && now - pressedAt_[other] <= window) {
      activePair_ = static_cast<int>(index / 2U);
      pairTriggered_ = false;
      chordStarted_ = now;
      pending_.fill(false);
    }
    return std::nullopt;
  }

  if (!down_[index]) {
    return std::nullopt;
  }
  down_[index] = false;
  if (cancelledUntilRelease_) {
    if (std::none_of(down_.begin(), down_.end(), [](bool down) { return down; })) {
      cancelledUntilRelease_ = false;
    }
    return std::nullopt;
  }
  if (activePair_ >= 0) {
    const auto first = static_cast<std::size_t>(activePair_ * 2);
    if (!down_[first] && !down_[first + 1]) {
      activePair_ = -1;
      pairTriggered_ = false;
    }
    pending_[index] = false;
    return std::nullopt;
  }
  if (pending_[index]) {
    pending_[index] = false;
    return selectAction(event.index);
  }
  return std::nullopt;
}

std::optional<FootswitchAction> FootswitchGesture::poll(Clock::time_point now)
{
  if (activePair_ >= 0 && !pairTriggered_) {
    const auto first = static_cast<std::size_t>(activePair_ * 2);
    const auto hold = activePair_ == 0 ? tunerHold : sceneLayerHold;
    if (down_[first] && down_[first + 1] && now - chordStarted_ >= hold) {
      pairTriggered_ = true;
      return FootswitchAction{
        activePair_ == 0 ? FootswitchActionType::ToggleTuner
                         : FootswitchActionType::ToggleSceneLayer,
        0,
      };
    }
  }

  const auto window = scenesAvailable_ && layerChordEnabled_ ? sceneChordWindow : chordWindow;
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (pending_[index] && down_[index]
        && !sceneLayer_ && static_cast<int>(index) == looperEntrySlot_
        && now - pressedAt_[index] >= looperHold) {
      pending_[index] = false;
      return FootswitchAction{FootswitchActionType::OpenLooper, static_cast<int>(index)};
    }
    if (pending_[index] && down_[index]
        && static_cast<int>(index) != looperEntrySlot_
        && now - pressedAt_[index] >= window) {
      pending_[index] = false;
      return FootswitchAction{
        sceneLayer_ ? FootswitchActionType::SelectScene : FootswitchActionType::SelectPreset,
        static_cast<int>(index),
      };
    }
  }
  return std::nullopt;
}

void FootswitchGesture::setLooperEntrySlot(int activePresetSlot)
{
  if (activePresetSlot < 0 || activePresetSlot >= static_cast<int>(down_.size())) {
    activePresetSlot = -1;
  }
  if (looperEntrySlot_ != activePresetSlot) {
    looperEntrySlot_ = activePresetSlot;
    reset();
  }
}

void FootswitchGesture::configureScenes(bool available, bool sceneLayer, bool layerChordEnabled)
{
  scenesAvailable_ = available;
  sceneLayer_ = available && sceneLayer;
  layerChordEnabled_ = layerChordEnabled;
  reset();
}

void FootswitchGesture::reset()
{
  down_.fill(false);
  pending_.fill(false);
  activePair_ = -1;
  pairTriggered_ = false;
  cancelledUntilRelease_ = false;
}

} // namespace ardor
