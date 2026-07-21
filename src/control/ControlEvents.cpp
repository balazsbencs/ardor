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
  if (event.type == ControlEventType::FootswitchPressed) {
    if (down_[index]) {
      return std::nullopt;
    }
    down_[index] = true;
    if (index >= 2) {
      return FootswitchAction{FootswitchActionType::SelectPreset, event.index};
    }

    pressedAt_[index] = now;
    pendingLeft_[index] = true;
    const auto other = 1U - index;
    if (down_[other]
        && now - pressedAt_[other] <= chordWindow) {
      chordActive_ = true;
      chordTriggered_ = false;
      chordStarted_ = now;
      pendingLeft_.fill(false);
    }
    return std::nullopt;
  }

  if (!down_[index]) {
    return std::nullopt;
  }
  down_[index] = false;
  if (index >= 2) {
    return std::nullopt;
  }
  if (chordActive_) {
    if (!down_[0] && !down_[1]) {
      chordActive_ = false;
      chordTriggered_ = false;
    }
    pendingLeft_[index] = false;
    return std::nullopt;
  }
  if (pendingLeft_[index]) {
    pendingLeft_[index] = false;
    return FootswitchAction{FootswitchActionType::SelectPreset, event.index};
  }
  return std::nullopt;
}

std::optional<FootswitchAction> FootswitchGesture::poll(Clock::time_point now)
{
  if (chordActive_ && !chordTriggered_ && down_[0] && down_[1]
      && now - chordStarted_ >= tunerHold) {
    chordTriggered_ = true;
    return FootswitchAction{FootswitchActionType::ToggleTuner, 0};
  }

  for (std::size_t index = 0; index < pendingLeft_.size(); ++index) {
    if (pendingLeft_[index] && down_[index] && now - pressedAt_[index] >= chordWindow) {
      pendingLeft_[index] = false;
      return FootswitchAction{FootswitchActionType::SelectPreset, static_cast<int>(index)};
    }
  }
  return std::nullopt;
}

void FootswitchGesture::reset()
{
  down_.fill(false);
  pendingLeft_.fill(false);
  chordActive_ = false;
  chordTriggered_ = false;
}

} // namespace ardor
