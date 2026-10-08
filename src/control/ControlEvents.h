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
  SelectScene,
  PreviousBank,
  NextBank,
  ToggleTuner,
  ToggleSceneLayer,
};

struct FootswitchAction {
  FootswitchActionType type = FootswitchActionType::SelectPreset;
  int index = 0;
};

// Turns raw press/release events into intentional pedal gestures. In Presets,
// FS2 and FS4 distinguish a tap for preset selection from a hold for bank
// navigation. Tuner and scene chords take priority over individual switches.
class FootswitchGesture {
public:
  using Clock = std::chrono::steady_clock;

  std::optional<FootswitchAction> handle(const ControlEvent& event, Clock::time_point now);
  std::optional<FootswitchAction> poll(Clock::time_point now);
  // Reconfiguring scene availability or the active layer consumes pending
  // events from the previous switch mapping.
  void configureScenes(bool available, bool sceneLayer, bool layerChordEnabled = true);
  void reset();

  static constexpr auto chordWindow = std::chrono::milliseconds(150);
  static constexpr auto sceneChordWindow = std::chrono::milliseconds(60);
  static constexpr auto tunerHold = std::chrono::milliseconds(1000);
  static constexpr auto sceneLayerHold = std::chrono::milliseconds(600);
  static constexpr auto bankHold = std::chrono::milliseconds(600);

private:
  std::array<bool, 4> down_{};
  std::array<bool, 4> pending_{};
  std::array<Clock::time_point, 4> pressedAt_{};
  int activePair_ = -1;
  bool pairTriggered_ = false;
  bool cancelledUntilRelease_ = false;
  Clock::time_point chordStarted_{};
  bool scenesAvailable_ = false;
  bool sceneLayer_ = false;
  bool layerChordEnabled_ = true;
};

} // namespace ardor
