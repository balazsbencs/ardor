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
  ToggleTuner,
  ToggleSceneLayer,
  OpenLooper,
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
  // Reconfiguring a layer consumes every pending press/release from the old
  // interpretation. Presets without scenes retain the legacy parser.
  void configureScenes(bool available, bool sceneLayer, bool layerChordEnabled = true);
  // -1 disables foot-only looper entry (for example while editing or in Scenes).
  void setLooperEntrySlot(int activePresetSlot);
  void reset();

  static constexpr auto chordWindow = std::chrono::milliseconds(150);
  static constexpr auto sceneChordWindow = std::chrono::milliseconds(60);
  static constexpr auto tunerHold = std::chrono::milliseconds(1000);
  static constexpr auto sceneLayerHold = std::chrono::milliseconds(600);
  static constexpr auto looperHold = std::chrono::milliseconds(1000);

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
  int looperEntrySlot_ = -1;
};

} // namespace ardor
