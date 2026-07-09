#pragma once

namespace ardor {

enum class ControlEventType {
  FootswitchPressed,
  EncoderTurned
};

struct ControlEvent {
  ControlEventType type = ControlEventType::FootswitchPressed;
  int index = 0;
  int delta = 0;
};

struct ControlState {
  int activeSlot = 0;
  int masterVolume = 80; // boot default: never full-scale into an amp on power-up
};

bool applyControlEvent(ControlState& state, const ControlEvent& event);

} // namespace ardor
