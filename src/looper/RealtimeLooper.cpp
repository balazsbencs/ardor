#include "looper/RealtimeLooper.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace ardor {
namespace {

constexpr uint32_t kSeamFrames = 32;

bool isCaptureState(LooperTrackState state) noexcept
{
  return state == LooperTrackState::ArmedRecord
      || state == LooperTrackState::Recording
      || state == LooperTrackState::ArmedOverdub
      || state == LooperTrackState::Overdubbing;
}

float dbToGain(float db) noexcept
{
  return db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

void balanceGains(float balance, float& left, float& right) noexcept
{
  balance = std::clamp(balance, -1.0f, 1.0f);
  left = balance <= 0.0f ? 1.0f : 1.0f - balance;
  right = balance >= 0.0f ? 1.0f : 1.0f + balance;
}

} // namespace

bool RealtimeLooper::prepare(float sampleRate,
                             std::size_t maximumBlockFrames,
                             std::size_t memoryBudgetBytes,
                             std::string& error)
{
  prepared_ = false;
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f || maximumBlockFrames == 0) {
    error = "invalid looper audio configuration";
    return false;
  }

  const std::size_t maximumFrames = memoryBudgetBytes / kBytesPerMasterFrame;
  if (maximumFrames < maximumBlockFrames * 2) {
    error = "looper memory budget cannot hold the minimum two-block loop";
    return false;
  }
  if (maximumFrames > static_cast<std::size_t>(std::numeric_limits<uint64_t>::max())) {
    error = "looper memory budget is too large";
    return false;
  }

  try {
    for (auto& track : tracks_) {
      track.baseLeft.assign(maximumFrames, 0.0f);
      track.baseRight.assign(maximumFrames, 0.0f);
      track.lastTakeLeft.assign(maximumFrames, 0.0f);
      track.lastTakeRight.assign(maximumFrames, 0.0f);
      clearTrackMetadata(track);
    }
  } catch (const std::bad_alloc&) {
    error = "unable to reserve looper audio memory";
    prepared_ = false;
    return false;
  }

  maximumFrames_ = maximumFrames;
  maximumBlockFrames_ = maximumBlockFrames;
  sampleRate_ = sampleRate;
  masterFrames_ = 0;
  playheadFrame_ = 0;
  sessionState_ = LooperSessionState::Inactive;
  error_ = LooperError::None;
  lastAppliedCommandSequence_ = 0;
  telemetryRevision_ = 0;
  contentRevision_ = 0;
  telemetryIntervalFrames_ = std::max<uint64_t>(1, static_cast<uint64_t>(sampleRate / 30.0f));
  framesUntilTelemetry_ = telemetryIntervalFrames_;
  smoothingCoefficient_ = 1.0f - std::exp(-1.0f / (sampleRate * 0.005f));
  prepared_ = true;
  stateChanged_ = false;
  telemetryPending_ = false;
  sessionOpen_.store(false, std::memory_order_release);
  return true;
}

bool RealtimeLooper::tryEnqueue(const LooperCommand& command) noexcept
{
  if (!prepared_ || !commands_.tryPush(command)) return false;
  if (command.type == LooperCommandType::OpenEmpty) {
    sessionOpen_.store(true, std::memory_order_release);
  }
  return true;
}

bool RealtimeLooper::tryReadTelemetry(LooperTelemetry& telemetry) noexcept
{
  return telemetry_.tryPop(telemetry);
}

bool RealtimeLooper::restorePausedSession(const LooperPausedSessionView& session,
                                          std::string& error)
{
  error.clear();
  if (!prepared_ || sessionState_ != LooperSessionState::Inactive
      || session.sampleRate != sampleRate_ || session.loopFrames == 0
      || session.loopFrames < maximumBlockFrames_ * 2
      || session.loopFrames > maximumFrames_) {
    error = "loaded loop does not fit the prepared realtime session";
    return false;
  }

  const auto frames = static_cast<std::size_t>(session.loopFrames);
  bool anyPresent = false;
  for (const auto& track : session.tracks) {
    if (!track.present) continue;
    anyPresent = true;
    if (track.baseLeft.size() < frames || track.baseRight.size() < frames
        || (track.lastTakeValid
            && (track.lastTakeLeft.size() < frames || track.lastTakeRight.size() < frames))
        || !std::isfinite(track.levelDb) || track.levelDb < -60.0f || track.levelDb > 6.0f
        || !std::isfinite(track.balance) || track.balance < -1.0f || track.balance > 1.0f) {
      error = "loaded loop track metadata is invalid";
      return false;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
      float left = track.baseLeft[frame];
      float right = track.baseRight[frame];
      if (track.lastTakeValid && track.lastTakeAudible) {
        left += track.lastTakeLeft[frame];
        right += track.lastTakeRight[frame];
      }
      if (!std::isfinite(left) || !std::isfinite(right)) {
        error = "loaded loop contains a non-finite sample";
        return false;
      }
    }
  }
  if (!anyPresent) {
    error = "loaded loop has no populated tracks";
    return false;
  }

  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    auto& destination = tracks_[index];
    const auto& source = session.tracks[index];
    clearTrackMetadata(destination);
    if (!source.present) continue;
    for (std::size_t frame = 0; frame < frames; ++frame) {
      destination.baseLeft[frame] = source.baseLeft[frame];
      destination.baseRight[frame] = source.baseRight[frame];
      if (source.lastTakeValid && source.lastTakeAudible) {
        destination.baseLeft[frame] += source.lastTakeLeft[frame];
        destination.baseRight[frame] += source.lastTakeRight[frame];
      }
    }
    destination.audible = !source.muted;
    destination.state = destination.audible ? LooperTrackState::Playing
                                            : LooperTrackState::Muted;
    destination.levelDb = source.levelDb;
    destination.targetGain = dbToGain(source.levelDb);
    destination.currentGain = destination.targetGain;
    destination.balance = source.balance;
    balanceGains(source.balance, destination.targetLeftPan, destination.targetRightPan);
    destination.currentLeftPan = destination.targetLeftPan;
    destination.currentRightPan = destination.targetRightPan;
  }

  masterFrames_ = session.loopFrames;
  playheadFrame_ = 0;
  sessionState_ = LooperSessionState::Paused;
  error_ = LooperError::None;
  stateChanged_ = true;
  telemetryPending_ = false;
  contentRevision_ = 1;
  sessionOpen_.store(true, std::memory_order_release);
  return true;
}

std::optional<LooperPausedSessionView> RealtimeLooper::pausedSessionView() const noexcept
{
  if (sessionState_ != LooperSessionState::Paused
      && sessionState_ != LooperSessionState::EmptyPaused) {
    return std::nullopt;
  }
  LooperPausedSessionView view;
  view.sampleRate = sampleRate_;
  view.loopFrames = masterFrames_;
  const auto frames = static_cast<std::size_t>(masterFrames_);
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    const auto& track = tracks_[index];
    auto& destination = view.tracks[index];
    destination.present = trackIsPopulated(track);
    destination.lastTakeValid = track.lastTakeValid;
    destination.lastTakeAudible = track.lastTakeAudible;
    destination.muted = destination.present && !track.audible;
    destination.levelDb = track.levelDb;
    destination.balance = track.balance;
    if (!destination.present) continue;
    destination.baseLeft = {track.baseLeft.data(), frames};
    destination.baseRight = {track.baseRight.data(), frames};
    destination.lastTakeLeft = {track.lastTakeLeft.data(), frames};
    destination.lastTakeRight = {track.lastTakeRight.data(), frames};
  }
  return view;
}

bool RealtimeLooper::trackIsPopulated(const Track& track) const noexcept
{
  return track.state != LooperTrackState::Empty
      && track.state != LooperTrackState::ArmedRecord
      && track.state != LooperTrackState::Recording;
}

bool RealtimeLooper::hasPopulatedTracks() const noexcept
{
  for (const auto& track : tracks_) {
    if (trackIsPopulated(track)) return true;
  }
  return false;
}

void RealtimeLooper::clearTrackMetadata(Track& track) noexcept
{
  track.state = LooperTrackState::Empty;
  track.audible = false;
  track.lastTakeValid = false;
  track.lastTakeAudible = false;
  track.capturedFrames = 0;
  track.levelDb = 0.0f;
  track.targetGain = 1.0f;
  track.currentGain = 1.0f;
  track.balance = 0.0f;
  track.targetLeftPan = 1.0f;
  track.targetRightPan = 1.0f;
  track.currentLeftPan = 1.0f;
  track.currentRightPan = 1.0f;
  track.seamLeft = 0.0f;
  track.seamRight = 0.0f;
  track.seamFramesRemaining = 0;
  track.seamLength = 0;
  track.lastRawLeft = 0.0f;
  track.lastRawRight = 0.0f;
}

void RealtimeLooper::clearTrack(std::size_t index) noexcept
{
  auto& track = tracks_[index];
  if (isCaptureState(track.state)) {
    error_ = LooperError::InvalidState;
    return;
  }
  const bool wasPopulated = trackIsPopulated(track);
  clearTrackMetadata(track);
  if (wasPopulated) ++contentRevision_;
  if (!hasPopulatedTracks()) {
    masterFrames_ = 0;
    playheadFrame_ = 0;
    sessionState_ = LooperSessionState::EmptyPaused;
  }
  stateChanged_ = true;
}

void RealtimeLooper::recordOrOverdub(std::size_t index) noexcept
{
  auto& track = tracks_[index];
  if (sessionState_ == LooperSessionState::RecordingMaster) {
    if (track.state == LooperTrackState::Recording) closeMasterRecording();
    else error_ = LooperError::InvalidState;
    return;
  }
  if (sessionState_ == LooperSessionState::EmptyPaused && masterFrames_ == 0) {
    if (track.state != LooperTrackState::Empty) {
      error_ = LooperError::InvalidState;
      return;
    }
    track.state = LooperTrackState::Recording;
    track.audible = true;
    track.capturedFrames = 0;
    sessionState_ = LooperSessionState::RecordingMaster;
    stateChanged_ = true;
    return;
  }

  if (sessionState_ != LooperSessionState::Running || masterFrames_ == 0) {
    error_ = LooperError::InvalidState;
    return;
  }

  if (track.state == LooperTrackState::ArmedRecord) {
    clearTrackMetadata(track);
  } else if (track.state == LooperTrackState::ArmedOverdub) {
    track.state = track.audible ? LooperTrackState::Playing : LooperTrackState::Muted;
  } else if (track.state == LooperTrackState::Empty) {
    track.state = LooperTrackState::ArmedRecord;
    track.audible = true;
    track.capturedFrames = 0;
  } else if (track.state == LooperTrackState::Playing
             || track.state == LooperTrackState::Muted) {
    track.state = LooperTrackState::ArmedOverdub;
    track.capturedFrames = 0;
  } else {
    error_ = LooperError::InvalidState;
    return;
  }
  stateChanged_ = true;
}

void RealtimeLooper::applyCommand(const LooperCommand& command) noexcept
{
  lastAppliedCommandSequence_ = command.sequence;
  error_ = LooperError::None;
  const bool usesTrack = command.type == LooperCommandType::RecordOrOverdub
      || command.type == LooperCommandType::ToggleTrackAudible
      || command.type == LooperCommandType::ToggleUndo
      || command.type == LooperCommandType::ClearTrack
      || command.type == LooperCommandType::SetTrackLevelDb
      || command.type == LooperCommandType::SetTrackBalance;
  if (usesTrack && command.track >= kLooperTrackCount) {
    error_ = LooperError::InvalidTrack;
    stateChanged_ = true;
    return;
  }

  auto& track = tracks_[command.track < kLooperTrackCount ? command.track : 0];
  switch (command.type) {
  case LooperCommandType::OpenEmpty:
    if (sessionState_ != LooperSessionState::Inactive) error_ = LooperError::InvalidState;
    else {
      contentRevision_ = 0;
      sessionState_ = LooperSessionState::EmptyPaused;
    }
    break;
  case LooperCommandType::RecordOrOverdub:
    recordOrOverdub(command.track);
    break;
  case LooperCommandType::ToggleTrackAudible:
    if (!trackIsPopulated(track) || isCaptureState(track.state)) {
      error_ = LooperError::InvalidState;
    } else {
      track.audible = !track.audible;
      track.state = track.audible ? LooperTrackState::Playing : LooperTrackState::Muted;
      ++contentRevision_;
    }
    break;
  case LooperCommandType::ToggleUndo:
    if (!track.lastTakeValid || isCaptureState(track.state)) error_ = LooperError::InvalidState;
    else {
      track.lastTakeAudible = !track.lastTakeAudible;
      ++contentRevision_;
    }
    break;
  case LooperCommandType::ClearTrack:
    clearTrack(command.track);
    break;
  case LooperCommandType::Pause:
    if (sessionState_ != LooperSessionState::Running) {
      error_ = LooperError::InvalidState;
    } else {
      for (const auto& candidate : tracks_) {
        if (candidate.state == LooperTrackState::Recording
            || candidate.state == LooperTrackState::Overdubbing) {
          error_ = LooperError::InvalidState;
          stateChanged_ = true;
          return;
        }
      }
      for (auto& candidate : tracks_) {
        if (candidate.state == LooperTrackState::ArmedRecord) clearTrackMetadata(candidate);
        else if (candidate.state == LooperTrackState::ArmedOverdub) {
          candidate.state = candidate.audible ? LooperTrackState::Playing
                                              : LooperTrackState::Muted;
        }
      }
      sessionState_ = LooperSessionState::Paused;
    }
    break;
  case LooperCommandType::Resume:
    if (sessionState_ != LooperSessionState::Paused) error_ = LooperError::InvalidState;
    else sessionState_ = masterFrames_ == 0 ? LooperSessionState::EmptyPaused
                                            : LooperSessionState::Running;
    break;
  case LooperCommandType::ResetSession:
    if (sessionState_ != LooperSessionState::Paused
        && sessionState_ != LooperSessionState::EmptyPaused) {
      error_ = LooperError::InvalidState;
    } else {
      const bool hadContent = hasPopulatedTracks();
      for (auto& candidate : tracks_) clearTrackMetadata(candidate);
      masterFrames_ = 0;
      playheadFrame_ = 0;
      sessionState_ = LooperSessionState::EmptyPaused;
      if (hadContent) ++contentRevision_;
    }
    break;
  case LooperCommandType::CloseSession:
    if (sessionState_ != LooperSessionState::Paused
        && sessionState_ != LooperSessionState::EmptyPaused) {
      error_ = LooperError::InvalidState;
    } else {
      for (auto& candidate : tracks_) clearTrackMetadata(candidate);
      masterFrames_ = 0;
      playheadFrame_ = 0;
      contentRevision_ = 0;
      sessionState_ = LooperSessionState::Inactive;
      sessionOpen_.store(false, std::memory_order_release);
    }
    break;
  case LooperCommandType::SetTrackLevelDb:
    if (isCaptureState(track.state) || !std::isfinite(command.value)) {
      error_ = LooperError::InvalidState;
    } else {
      const float levelDb = std::clamp(command.value, -60.0f, 6.0f);
      if (trackIsPopulated(track) && levelDb != track.levelDb) ++contentRevision_;
      track.levelDb = levelDb;
      track.targetGain = dbToGain(track.levelDb);
    }
    break;
  case LooperCommandType::SetTrackBalance:
    if (isCaptureState(track.state) || !std::isfinite(command.value)) {
      error_ = LooperError::InvalidState;
    } else {
      const float balance = std::clamp(command.value, -1.0f, 1.0f);
      if (trackIsPopulated(track) && balance != track.balance) ++contentRevision_;
      track.balance = balance;
      balanceGains(track.balance, track.targetLeftPan, track.targetRightPan);
    }
    break;
  }
  stateChanged_ = true;
}

void RealtimeLooper::consumeCommands() noexcept
{
  LooperCommand command;
  while (commands_.tryPop(command)) applyCommand(command);
}

void RealtimeLooper::effectiveSample(const Track& track,
                                     std::size_t frame,
                                     float& left,
                                     float& right) const noexcept
{
  left = track.baseLeft[frame];
  right = track.baseRight[frame];
  if (track.lastTakeValid && track.lastTakeAudible) {
    left += track.lastTakeLeft[frame];
    right += track.lastTakeRight[frame];
  }
}

void RealtimeLooper::closeMasterRecording(bool maximumLengthReached) noexcept
{
  Track* recording = nullptr;
  for (auto& track : tracks_) {
    if (track.state == LooperTrackState::Recording) {
      recording = &track;
      break;
    }
  }
  if (recording == nullptr) return;

  if (recording->capturedFrames < maximumBlockFrames_ * 2) {
    clearTrackMetadata(*recording);
    sessionState_ = LooperSessionState::EmptyPaused;
    masterFrames_ = 0;
    error_ = LooperError::MasterTooShort;
  } else {
    masterFrames_ = recording->capturedFrames;
    recording->state = LooperTrackState::Playing;
    recording->audible = true;
    playheadFrame_ = 0;
    sessionState_ = LooperSessionState::Running;
    ++contentRevision_;
    if (maximumLengthReached) error_ = LooperError::MaximumLengthReached;
  }
  stateChanged_ = true;
}

void RealtimeLooper::startArmedTracksAtBoundary() noexcept
{
  if (playheadFrame_ != 0) return;
  for (auto& track : tracks_) {
    if (track.state == LooperTrackState::ArmedRecord) {
      track.state = LooperTrackState::Recording;
      track.capturedFrames = 0;
      stateChanged_ = true;
    } else if (track.state == LooperTrackState::ArmedOverdub) {
      track.state = LooperTrackState::Overdubbing;
      track.capturedFrames = 0;
      stateChanged_ = true;
    }
  }
}

void RealtimeLooper::finishFollowerCapture(Track& track) noexcept
{
  if (track.state == LooperTrackState::Overdubbing) {
    track.lastTakeValid = true;
    track.lastTakeAudible = true;
  }
  track.state = track.audible ? LooperTrackState::Playing : LooperTrackState::Muted;
  track.capturedFrames = 0;
  ++contentRevision_;
  stateChanged_ = true;
}

void RealtimeLooper::updateSeamsAtWrap() noexcept
{
  if (masterFrames_ == 0) return;
  const uint32_t length = static_cast<uint32_t>(std::min<uint64_t>(
      kSeamFrames, std::max<uint64_t>(1, masterFrames_ / 2)));
  for (auto& track : tracks_) {
    if (!trackIsPopulated(track)) continue;
    float firstLeft = 0.0f;
    float firstRight = 0.0f;
    effectiveSample(track, 0, firstLeft, firstRight);
    track.seamLeft = track.lastRawLeft - firstLeft;
    track.seamRight = track.lastRawRight - firstRight;
    track.seamFramesRemaining = length;
    track.seamLength = length;
  }
}

LooperTelemetry RealtimeLooper::makeTelemetry() const noexcept
{
  LooperTelemetry value;
  value.revision = telemetryRevision_ + 1;
  value.contentRevision = contentRevision_;
  value.lastAppliedCommandSequence = lastAppliedCommandSequence_;
  value.sessionState = sessionState_;
  value.error = error_;
  value.masterFrames = masterFrames_;
  value.maximumFrames = maximumFrames_;
  value.playheadFrame = playheadFrame_;
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    const auto& source = tracks_[index];
    auto& destination = value.tracks[index];
    destination.state = source.state;
    destination.audible = source.audible;
    destination.undoAvailable = source.lastTakeValid;
    destination.undoApplied = source.lastTakeValid && !source.lastTakeAudible;
    destination.levelDb = source.levelDb;
    destination.balance = source.balance;
  }
  return value;
}

void RealtimeLooper::publishTelemetry(bool force) noexcept
{
  if (telemetryPending_) {
    if (telemetry_.tryPush(pendingTelemetry_)) {
      telemetryRevision_ = pendingTelemetry_.revision;
      telemetryPending_ = false;
    }
  }
  if (!force) return;

  auto value = makeTelemetry();
  if (telemetry_.tryPush(value)) telemetryRevision_ = value.revision;
  else {
    pendingTelemetry_ = value;
    telemetryPending_ = true;
  }
}

void RealtimeLooper::processBlock(float* left, float* right, std::size_t frames) noexcept
{
  if (!prepared_ || left == nullptr || right == nullptr || frames == 0) return;
  consumeCommands();

  if (sessionState_ == LooperSessionState::Inactive
      || sessionState_ == LooperSessionState::EmptyPaused
      || sessionState_ == LooperSessionState::Paused
      || sessionState_ == LooperSessionState::Faulted) {
    publishTelemetry(stateChanged_);
    stateChanged_ = false;
    return;
  }

  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float liveLeft = left[frame];
    const float liveRight = right[frame];

    if (sessionState_ == LooperSessionState::RecordingMaster) {
      for (auto& track : tracks_) {
        if (track.state != LooperTrackState::Recording) continue;
        const auto position = static_cast<std::size_t>(track.capturedFrames);
        track.baseLeft[position] = liveLeft;
        track.baseRight[position] = liveRight;
        ++track.capturedFrames;
        if (track.capturedFrames == maximumFrames_) closeMasterRecording(true);
        break;
      }
      continue;
    }

    startArmedTracksAtBoundary();
    float outputLeft = liveLeft;
    float outputRight = liveRight;
    const auto position = static_cast<std::size_t>(playheadFrame_);
    for (auto& track : tracks_) {
      track.currentGain += (track.targetGain - track.currentGain) * smoothingCoefficient_;
      track.currentLeftPan += (track.targetLeftPan - track.currentLeftPan) * smoothingCoefficient_;
      track.currentRightPan += (track.targetRightPan - track.currentRightPan) * smoothingCoefficient_;

      if (track.state == LooperTrackState::Recording) {
        track.baseLeft[position] = liveLeft;
        track.baseRight[position] = liveRight;
        if (++track.capturedFrames == masterFrames_) finishFollowerCapture(track);
        continue;
      }

      float loopLeft = 0.0f;
      float loopRight = 0.0f;
      if (trackIsPopulated(track)) {
        effectiveSample(track, position, loopLeft, loopRight);
        if (track.state == LooperTrackState::Overdubbing) {
          track.baseLeft[position] = loopLeft;
          track.baseRight[position] = loopRight;
          track.lastTakeLeft[position] = liveLeft;
          track.lastTakeRight[position] = liveRight;
          if (++track.capturedFrames == masterFrames_) finishFollowerCapture(track);
        }

        track.lastRawLeft = loopLeft;
        track.lastRawRight = loopRight;
        if (track.seamFramesRemaining > 0) {
          const float scale = static_cast<float>(track.seamFramesRemaining)
              / static_cast<float>(track.seamLength);
          loopLeft += track.seamLeft * scale;
          loopRight += track.seamRight * scale;
          --track.seamFramesRemaining;
        }
        if (track.audible) {
          outputLeft += loopLeft * track.currentGain * track.currentLeftPan;
          outputRight += loopRight * track.currentGain * track.currentRightPan;
        }
      }
    }
    left[frame] = outputLeft;
    right[frame] = outputRight;

    if (++playheadFrame_ == masterFrames_) {
      playheadFrame_ = 0;
      updateSeamsAtWrap();
    }
  }

  if (framesUntilTelemetry_ <= frames) {
    framesUntilTelemetry_ = telemetryIntervalFrames_;
    stateChanged_ = true;
  } else {
    framesUntilTelemetry_ -= frames;
  }
  publishTelemetry(stateChanged_);
  stateChanged_ = false;
}

} // namespace ardor
