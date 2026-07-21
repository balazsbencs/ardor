#pragma once

#include <array>
#include <chrono>
#include <optional>

namespace ardor {

enum class ControlEventType {
  FootswitchPressed,
  FootswitchReleased,
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

enum class FootswitchActionType {
  SelectPreset,
  ToggleTuner,
};

struct FootswitchAction {
  FootswitchActionType type = FootswitchActionType::SelectPreset;
  int index = 0;
};

// Turns raw press/release events into intentional pedal gestures. The two
// left switches (0 and 1) get a short chord-detection window so beginning the
// tuner gesture cannot accidentally change presets. The right switches remain
// immediate.
class FootswitchGesture {
public:
  using Clock = std::chrono::steady_clock;

  std::optional<FootswitchAction> handle(const ControlEvent& event, Clock::time_point now);
  std::optional<FootswitchAction> poll(Clock::time_point now);
  void reset();

  static constexpr auto chordWindow = std::chrono::milliseconds(150);
  static constexpr auto tunerHold = std::chrono::milliseconds(1000);

private:
  std::array<bool, 4> down_{};
  std::array<bool, 2> pendingLeft_{};
  std::array<Clock::time_point, 2> pressedAt_{};
  bool chordActive_ = false;
  bool chordTriggered_ = false;
  Clock::time_point chordStarted_{};
};

} // namespace ardor
