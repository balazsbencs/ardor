#include "looper/LooperController.h"

#include <algorithm>

namespace ardor {

LooperControllerAction LooperController::command(LooperCommandType type,
                                                 std::size_t track,
                                                 float value) noexcept
{
  return {LooperControllerActionType::Command,
          {nextSequence_++, type, static_cast<uint8_t>(track), value}};
}

std::optional<LooperControllerAction> LooperController::openSession()
{
  if (sessionLocked_) return std::nullopt;
  sessionLocked_ = true;
  selectedTrack_ = 0;
  return command(LooperCommandType::OpenEmpty);
}

std::optional<LooperControllerAction> LooperController::closeSession()
{
  if (!sessionLocked_ || (telemetry_.sessionState != LooperSessionState::Paused
                           && telemetry_.sessionState != LooperSessionState::EmptyPaused)) {
    return std::nullopt;
  }
  auto action = command(LooperCommandType::CloseSession);
  closeSequence_ = action.command.sequence;
  return action;
}

std::optional<LooperControllerAction> LooperController::requestCommand(
    LooperCommandType type, std::size_t track, float value)
{
  if (!sessionLocked_ || track >= kLooperTrackCount) return std::nullopt;
  return command(type, track, value);
}

std::optional<LooperControllerAction> LooperController::requestTunerMode()
{
  return requestTuner();
}

void LooperController::selectTrack(std::size_t track) noexcept
{
  if (track < kLooperTrackCount) selectedTrack_ = track;
}

void LooperController::submissionFailed(const LooperControllerAction& action) noexcept
{
  if (action.type != LooperControllerActionType::Command) return;
  if (action.command.type == LooperCommandType::OpenEmpty
      && telemetry_.sessionState == LooperSessionState::Inactive) {
    sessionLocked_ = false;
    resetGestures();
  }
  if (action.command.sequence == closeSequence_) closeSequence_ = 0;
  if (action.command.sequence == tunerPauseSequence_) {
    tunerPauseSequence_ = 0;
    tunerPending_ = false;
    tunerReady_ = false;
    resumeAfterTuner_ = false;
  }
}

void LooperController::updateTelemetry(const LooperTelemetry& telemetry) noexcept
{
  telemetry_ = telemetry;
  if (closeSequence_ > 0
      && telemetry.sessionState == LooperSessionState::Inactive
      && telemetry.lastAppliedCommandSequence >= closeSequence_) {
    sessionLocked_ = false;
    closeSequence_ = 0;
  } else if (telemetry.sessionState != LooperSessionState::Inactive) {
    sessionLocked_ = true;
  }

  if (tunerPending_
      && telemetry.lastAppliedCommandSequence >= tunerPauseSequence_) {
    tunerPending_ = false;
    if (telemetry.sessionState == LooperSessionState::Paused) tunerReady_ = true;
    else resumeAfterTuner_ = false;
  }
}

std::optional<LooperControllerAction> LooperController::requestTuner()
{
  if (tunerActive_ || tunerPending_ || tunerReady_) return std::nullopt;
  const bool captureActive = telemetry_.sessionState == LooperSessionState::RecordingMaster
    || std::any_of(telemetry_.tracks.begin(), telemetry_.tracks.end(), [](const auto& track) {
      return track.state == LooperTrackState::Recording
          || track.state == LooperTrackState::Overdubbing;
    });
  if (captureActive) {
    auto action = command(LooperCommandType::Pause);
    tunerPauseSequence_ = action.command.sequence;
    tunerPending_ = true;
    resumeAfterTuner_ = false;
    return action;
  }
  resumeAfterTuner_ = telemetry_.sessionState == LooperSessionState::Running;
  if (resumeAfterTuner_) {
    auto action = command(LooperCommandType::Pause);
    tunerPauseSequence_ = action.command.sequence;
    tunerPending_ = true;
    return action;
  }
  tunerActive_ = true;
  return LooperControllerAction{LooperControllerActionType::EnterTuner, {}};
}

std::optional<LooperControllerAction> LooperController::handleFootswitch(
    const ControlEvent& event, Clock::time_point now)
{
  if (event.type != ControlEventType::FootswitchPressed
      && event.type != ControlEventType::FootswitchReleased) {
    return std::nullopt;
  }
  if (event.index < 0 || event.index >= static_cast<int>(down_.size())) return std::nullopt;
  const auto index = static_cast<std::size_t>(event.index);

  if (tunerActive_) {
    if (event.type == ControlEventType::FootswitchPressed) return leaveTuner();
    return std::nullopt;
  }
  if (!sessionLocked_) return std::nullopt;

  if (event.type == ControlEventType::FootswitchPressed) {
    if (down_[index]) return std::nullopt;
    down_[index] = true;
    if (index == 2) return command(LooperCommandType::RecordOrOverdub, selectedTrack_);
    if (index == 3) return command(LooperCommandType::ToggleTrackAudible, selectedTrack_);

    pressedAt_[index] = now;
    pendingLeft_[index] = true;
    if (down_[1U - index] && now - pressedAt_[1U - index] <= chordWindow) {
      chordActive_ = true;
      chordTriggered_ = false;
      chordStarted_ = now;
      pendingLeft_.fill(false);
    }
    return std::nullopt;
  }

  if (!down_[index]) return std::nullopt;
  down_[index] = false;
  if (index >= 2) return std::nullopt;
  if (chordActive_) {
    if (!down_[0] && !down_[1]) {
      chordActive_ = false;
      chordTriggered_ = false;
    }
    pendingLeft_[index] = false;
    return std::nullopt;
  }

  if (index == 1 && clearTriggered_) {
    clearTriggered_ = false;
    pendingLeft_[index] = false;
    return std::nullopt;
  }
  const bool pending = pendingLeft_[index];
  pendingLeft_[index] = false;
  if (!pending) return std::nullopt;
  if (index == 0) return command(LooperCommandType::ToggleUndo, selectedTrack_);
  selectedTrack_ = (selectedTrack_ + 1) % kLooperTrackCount;
  return std::nullopt;
}

std::optional<LooperControllerAction> LooperController::poll(Clock::time_point now)
{
  if (tunerReady_) {
    tunerReady_ = false;
    tunerActive_ = true;
    return LooperControllerAction{LooperControllerActionType::EnterTuner, {}};
  }
  if (chordActive_ && !chordTriggered_ && down_[0] && down_[1]
      && now - chordStarted_ >= tunerHold) {
    chordTriggered_ = true;
    return requestTuner();
  }
  if (down_[1] && pendingLeft_[1] && !clearTriggered_
      && now - pressedAt_[1] >= clearHold) {
    clearTriggered_ = true;
    pendingLeft_[1] = false;
    return command(LooperCommandType::ClearTrack, selectedTrack_);
  }
  return std::nullopt;
}

std::optional<LooperControllerAction> LooperController::leaveTuner()
{
  if (!tunerActive_) return std::nullopt;
  tunerActive_ = false;
  resetGestures();
  if (resumeAfterTuner_) {
    resumeAfterTuner_ = false;
    return command(LooperCommandType::Resume);
  }
  return std::nullopt;
}

float LooperController::clearHoldProgress(Clock::time_point now) const noexcept
{
  if (!down_[1] || !pendingLeft_[1] || chordActive_) return 0.0f;
  const auto elapsed = now - pressedAt_[1];
  if (elapsed <= Clock::duration::zero()) return 0.0f;
  return std::clamp(
      std::chrono::duration<float>(elapsed).count()
          / std::chrono::duration<float>(clearHold).count(),
      0.0f, 1.0f);
}

void LooperController::resetGestures() noexcept
{
  down_.fill(false);
  pendingLeft_.fill(false);
  chordActive_ = false;
  chordTriggered_ = false;
  clearTriggered_ = false;
}

} // namespace ardor
