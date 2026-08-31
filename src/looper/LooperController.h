#pragma once

#include "control/ControlEvents.h"
#include "looper/RealtimeLooper.h"

#include <array>
#include <chrono>
#include <optional>

namespace ardor {

enum class LooperControllerActionType : uint8_t {
  Command,
  EnterTuner,
};

struct LooperControllerAction {
  LooperControllerActionType type = LooperControllerActionType::Command;
  LooperCommand command {};
};

// Control-thread gesture/state adapter. It does not call the engine itself;
// the application submits returned commands and feeds audio acknowledgements
// back through updateTelemetry().
class LooperController {
public:
  using Clock = std::chrono::steady_clock;

  std::optional<LooperControllerAction> openSession();
  std::optional<LooperControllerAction> closeSession();
  std::optional<LooperControllerAction> requestCommand(LooperCommandType type,
                                                        std::size_t track = 0,
                                                        float value = 0.0f);
  std::optional<LooperControllerAction> requestTunerMode();
  void selectTrack(std::size_t track) noexcept;
  std::optional<LooperControllerAction> handleFootswitch(const ControlEvent& event,
                                                          Clock::time_point now);
  std::optional<LooperControllerAction> poll(Clock::time_point now);
  std::optional<LooperControllerAction> leaveTuner();
  // Roll back controller-only reservations when the host cannot publish a
  // returned command to the realtime queue.
  void submissionFailed(const LooperControllerAction& action) noexcept;
  void updateTelemetry(const LooperTelemetry& telemetry) noexcept;
  void resetGestures() noexcept;

  std::size_t selectedTrack() const noexcept { return selectedTrack_; }
  bool sessionLocked() const noexcept { return sessionLocked_; }
  bool tunerActive() const noexcept { return tunerActive_; }
  float clearHoldProgress(Clock::time_point now) const noexcept;
  const LooperTelemetry& telemetry() const noexcept { return telemetry_; }

  static constexpr auto chordWindow = std::chrono::milliseconds(150);
  static constexpr auto tunerHold = std::chrono::milliseconds(1000);
  static constexpr auto clearHold = std::chrono::milliseconds(1500);

private:
  LooperControllerAction command(LooperCommandType type, std::size_t track = 0,
                                 float value = 0.0f) noexcept;
  std::optional<LooperControllerAction> requestTuner();

  std::array<bool, kLooperTrackCount> down_ {};
  std::array<Clock::time_point, 2> pressedAt_ {};
  std::array<bool, 2> pendingLeft_ {};
  LooperTelemetry telemetry_ {};
  uint64_t nextSequence_ = 1;
  uint64_t closeSequence_ = 0;
  uint64_t tunerPauseSequence_ = 0;
  std::size_t selectedTrack_ = 0;
  bool chordActive_ = false;
  bool chordTriggered_ = false;
  bool clearTriggered_ = false;
  bool sessionLocked_ = false;
  bool tunerPending_ = false;
  bool tunerReady_ = false;
  bool tunerActive_ = false;
  bool resumeAfterTuner_ = false;
  Clock::time_point chordStarted_ {};
};

} // namespace ardor
