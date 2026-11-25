#include "dsp/SceneTransition.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace ardor {

void SceneRequestMailbox::reset() noexcept
{
  serial_.store(0, std::memory_order_relaxed);
  presetGeneration_.store(0, std::memory_order_relaxed);
  requestId_.store(0, std::memory_order_relaxed);
  durationFrames_.store(0, std::memory_order_relaxed);
  sceneIndex_.store(0, std::memory_order_relaxed);
}

void SceneRequestMailbox::publish(const SceneTransitionRequest& request) noexcept
{
  serial_.fetch_add(1, std::memory_order_acq_rel);
  presetGeneration_.store(request.presetGeneration, std::memory_order_relaxed);
  requestId_.store(request.requestId, std::memory_order_relaxed);
  durationFrames_.store(request.durationFrames, std::memory_order_relaxed);
  sceneIndex_.store(request.sceneIndex, std::memory_order_relaxed);
  serial_.fetch_add(1, std::memory_order_release);
}

bool SceneRequestMailbox::readLatest(std::uint64_t& lastSerial,
                                     SceneTransitionRequest& request) const noexcept
{
  const auto before = serial_.load(std::memory_order_acquire);
  if (before == lastSerial || (before & 1U) != 0) return false;
  SceneTransitionRequest candidate;
  candidate.presetGeneration = presetGeneration_.load(std::memory_order_relaxed);
  candidate.requestId = requestId_.load(std::memory_order_relaxed);
  candidate.durationFrames = durationFrames_.load(std::memory_order_relaxed);
  candidate.sceneIndex = sceneIndex_.load(std::memory_order_relaxed);
  const auto after = serial_.load(std::memory_order_acquire);
  if (before != after || (after & 1U) != 0) return false;
  request = candidate;
  lastSerial = after;
  return true;
}

bool SceneTransitionController::prepare(SceneTransitionProgram program)
{
  prepared_ = false;
  if (program.targets.size() > kMaximumTargets || program.defaultSceneIndex >= 4) return false;
  for (const auto& target : program.targets) {
    for (const float value : target.values) {
      if (!std::isfinite(value)) return false;
    }
  }
  for (const float trim : program.outputTrimDb) {
    if (!std::isfinite(trim) || trim < -12.0f || trim > 6.0f) return false;
  }

  try {
    program_ = std::move(program);
    currentValues_.resize(program_.targets.size());
    startValues_.resize(program_.targets.size());
    destinationValues_.resize(program_.targets.size());
    overridden_.assign(program_.targets.size(), false);
  } catch (...) {
    program_ = {};
    currentValues_.clear();
    startValues_.clear();
    destinationValues_.clear();
    return false;
  }

  currentSceneIndex_ = program_.defaultSceneIndex;
  destinationSceneIndex_ = currentSceneIndex_;
  for (std::size_t index = 0; index < program_.targets.size(); ++index) {
    currentValues_[index] = program_.targets[index].values[currentSceneIndex_];
  }
  startValues_ = currentValues_;
  destinationValues_ = currentValues_;
  currentOutputTrimDb_ = program_.outputTrimDb[currentSceneIndex_];
  startOutputTrimDb_ = currentOutputTrimDb_;
  destinationOutputTrimDb_ = currentOutputTrimDb_;
  mailbox_.reset();
  bank_.serial.store(0);
  consumedBankSerial_ = 0;
  for (auto& bits : overrideBits_) bits.store(0, std::memory_order_relaxed);
  consumedSerial_ = 0;
  lastAppliedRequestId_ = 0;
  elapsedFrames_ = 0;
  totalFrames_ = 0;
  transitioning_ = false;
  prepared_ = true;
  publishTelemetry();
  return true;
}

bool SceneTransitionController::request(const SceneTransitionRequest& request) noexcept
{
  if (!prepared_ || request.sceneIndex >= 4 || request.requestId == 0) return false;
  mailbox_.publish(request);
  return true;
}

bool SceneTransitionController::requestOverride(std::size_t targetIndex, float value) noexcept
{
  if (!prepared_ || targetIndex >= program_.targets.size() || !std::isfinite(value)) return false;
  overrideValues_[targetIndex].store(value, std::memory_order_relaxed);
  overrideBits_[targetIndex / 64].fetch_or(
    std::uint64_t{1} << (targetIndex % 64), std::memory_order_release);
  return true;
}

bool SceneTransitionController::publishSceneBank(const SceneTransitionProgram& program) noexcept
{
  if (!prepared_ || program.targets.size() != program_.targets.size()) return false;
  for (std::size_t index = 0; index < program.targets.size(); ++index) {
    if (program.targets[index].address != program_.targets[index].address
        || program.targets[index].law != program_.targets[index].law) return false;
  }
  // A single producer writes the complete authored bank. The audio thread
  // copies it only when the sequence changes and never waits for publication.
  bank_.serial.fetch_add(1);
  for (std::size_t index = 0; index < program.targets.size(); ++index) {
    for (std::size_t scene = 0; scene < 4; ++scene)
      bank_.values[index * 4 + scene].store(program.targets[index].values[scene]);
  }
  for (std::size_t scene = 0; scene < 4; ++scene) {
    bank_.trims[scene].store(program.outputTrimDb[scene]);
    bank_.identities[scene].store(program.sceneIdentities[scene]);
  }
  bank_.serial.fetch_add(1);
  return true;
}

bool SceneTransitionController::consumeSceneBank() noexcept
{
  const auto serial = bank_.serial.load();
  if (serial == consumedBankSerial_) return true;
  if (serial & 1U) return false;
  std::array<std::array<float, 4>, kMaximumTargets> values;
  std::array<float, 4> trims;
  std::array<std::uint64_t, 4> identities;
  for (std::size_t index = 0; index < program_.targets.size(); ++index) {
    for (std::size_t scene = 0; scene < 4; ++scene)
      values[index][scene] = bank_.values[index * 4 + scene].load();
  }
  for (std::size_t scene = 0; scene < 4; ++scene) {
    trims[scene] = bank_.trims[scene].load();
    identities[scene] = bank_.identities[scene].load();
  }
  if (bank_.serial.load() != serial) return false;
  consumedBankSerial_ = serial;

  const auto remap = [&](std::uint8_t scene) {
    const auto found = std::find(identities.begin(), identities.end(), program_.sceneIdentities[scene]);
    return found == identities.end() ? scene
      : static_cast<std::uint8_t>(std::distance(identities.begin(), found));
  };
  const auto destination = remap(destinationSceneIndex_);
  const auto previousDestination = destinationSceneIndex_;
  currentSceneIndex_ = remap(currentSceneIndex_);
  destinationSceneIndex_ = destination;
  bool destinationChanged = program_.outputTrimDb[previousDestination] != trims[destination];
  std::array<bool, kMaximumTargets> keepOverride{};
  std::array<float, kMaximumTargets> overrideValue;
  for (std::size_t index = 0; index < program_.targets.size(); ++index) {
    auto& target = program_.targets[index];
    const bool changed = target.values[previousDestination] != values[index][destination];
    destinationChanged |= changed;
    keepOverride[index] = overridden_[index] && !changed;
    if (keepOverride[index]) overrideValue[index] = currentValues_[index];
    target.values = values[index];
  }
  program_.sceneIdentities = identities;
  program_.outputTrimDb = trims;
  if (destinationChanged) {
    const auto remaining = transitioning_ ? totalFrames_ - elapsedFrames_ : 0;
    startRequest({program_.presetGeneration, lastAppliedRequestId_, remaining, destination});
    for (std::size_t index = 0; index < program_.targets.size(); ++index) {
      if (!keepOverride[index]) continue;
      overridden_[index] = true;
      currentValues_[index] = startValues_[index] = destinationValues_[index] = overrideValue[index];
    }
  }
  publishTelemetry();
  return true;
}

bool SceneTransitionController::hasOverrides() const noexcept
{
  return std::any_of(overridden_.begin(), overridden_.end(), [](bool value) { return value; });
}

void SceneTransitionController::beginBlock() noexcept
{
  if (!prepared_ || !consumeSceneBank()) return;
  SceneTransitionRequest request;
  if (mailbox_.readLatest(consumedSerial_, request)
      && request.presetGeneration == program_.presetGeneration
      && request.requestId > lastAppliedRequestId_) {
    const bool sameDestination = request.sceneIndex == destinationSceneIndex_;
    if (sameDestination && (transitioning_ || !hasOverrides())) {
      lastAppliedRequestId_ = request.requestId;
      publishTelemetry();
    } else {
      startRequest(request);
    }
  }
  consumeOverrides();
}

void SceneTransitionController::consumeOverrides() noexcept
{
  for (std::size_t word = 0; word < overrideBits_.size(); ++word) {
    auto bits = overrideBits_[word].exchange(0, std::memory_order_acquire);
    while (bits != 0) {
      const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
      const auto index = word * 64 + bit;
      if (index < currentValues_.size()) {
        const float value = overrideValues_[index].load(std::memory_order_relaxed);
        currentValues_[index] = value;
        startValues_[index] = value;
        destinationValues_[index] = value;
        overridden_[index] = true;
      }
      bits &= bits - 1;
    }
  }
}

void SceneTransitionController::startRequest(const SceneTransitionRequest& request) noexcept
{
  lastAppliedRequestId_ = request.requestId;
  destinationSceneIndex_ = request.sceneIndex;
  elapsedFrames_ = 0;
  totalFrames_ = request.durationFrames;
  startValues_ = currentValues_;
  startOutputTrimDb_ = currentOutputTrimDb_;
  destinationOutputTrimDb_ = program_.outputTrimDb[destinationSceneIndex_];
  std::fill(overridden_.begin(), overridden_.end(), false);
  for (std::size_t index = 0; index < program_.targets.size(); ++index) {
    destinationValues_[index] = program_.targets[index].values[destinationSceneIndex_];
  }

  if (totalFrames_ == 0 && program_.instantGainFrames != 0) {
    bool smoothGain = startOutputTrimDb_ != destinationOutputTrimDb_;
    for (std::size_t index = 0; index < program_.targets.size(); ++index) {
      if (program_.targets[index].address.kind == SceneRuntimeTargetKind::InputGainDb) {
        smoothGain |= currentValues_[index] != destinationValues_[index];
      } else {
        currentValues_[index] = destinationValues_[index];
        startValues_[index] = destinationValues_[index];
      }
    }
    if (smoothGain) totalFrames_ = program_.instantGainFrames;
  }
  if (totalFrames_ == 0) {
    currentValues_ = destinationValues_;
    currentOutputTrimDb_ = destinationOutputTrimDb_;
    currentSceneIndex_ = destinationSceneIndex_;
    transitioning_ = false;
    publishTelemetry();
    return;
  }
  for (std::size_t index = 0; index < program_.targets.size(); ++index) {
    if (program_.targets[index].law == SceneTransitionLaw::Stepped) {
      currentValues_[index] = destinationValues_[index];
    }
  }
  transitioning_ = true;
  publishTelemetry();
}

float SceneTransitionController::interpolate(SceneTransitionLaw law, float start, float end,
                                             float position) noexcept
{
  position = std::clamp(position, 0.0f, 1.0f);
  if (law == SceneTransitionLaw::Stepped) return end;
  if (law == SceneTransitionLaw::LogFrequency && start > 0.0f && end > 0.0f) {
    return std::exp(std::log(start) + (std::log(end) - std::log(start)) * position);
  }
  // Values expressed in decibels interpolate linearly in their stored domain;
  // conversion to amplitude happens only at the final DSP dispatch point.
  return start + (end - start) * position;
}

void SceneTransitionController::advanceFrame() noexcept
{
  if (!prepared_ || !transitioning_) return;
  ++elapsedFrames_;
  const float position = std::min(1.0f,
    static_cast<float>(elapsedFrames_) / static_cast<float>(totalFrames_));
  for (std::size_t index = 0; index < program_.targets.size(); ++index) {
    if (overridden_[index]) continue;
    currentValues_[index] = interpolate(program_.targets[index].law, startValues_[index],
                                        destinationValues_[index], position);
  }
  currentOutputTrimDb_ = interpolate(SceneTransitionLaw::Decibels, startOutputTrimDb_,
                                     destinationOutputTrimDb_, position);
  if (elapsedFrames_ >= totalFrames_) {
    currentSceneIndex_ = destinationSceneIndex_;
    transitioning_ = false;
  }
  // UI telemetry needs millisecond resolution, not an atomic publication for
  // every audio frame. Always publish the final frame.
  if ((elapsedFrames_ & 63U) == 0 || !transitioning_) publishTelemetry();
}

void SceneTransitionController::publishTelemetry() noexcept
{
  // One total order covers the serial and every field: a reader cannot accept
  // a mixed snapshot if publication overlaps its two serial loads.
  telemetrySerial_.fetch_add(1);
  telemetryRequestId_.store(lastAppliedRequestId_);
  telemetryCurrentScene_.store(currentSceneIndex_);
  telemetryDestinationScene_.store(destinationSceneIndex_);
  telemetryElapsedFrames_.store(elapsedFrames_);
  telemetryTotalFrames_.store(totalFrames_);
  telemetryTransitioning_.store(transitioning_);
  telemetryCurrentIdentity_.store(program_.sceneIdentities[currentSceneIndex_]);
  telemetryDestinationIdentity_.store(program_.sceneIdentities[destinationSceneIndex_]);
  telemetrySerial_.fetch_add(1);
}

SceneTransitionTelemetry SceneTransitionController::telemetry() const noexcept
{
  SceneTransitionTelemetry snapshot;
  for (int attempt = 0; attempt < 4; ++attempt) {
    const auto before = telemetrySerial_.load();
    if ((before & 1U) != 0) continue;
    snapshot.lastAppliedRequestId = telemetryRequestId_.load();
    snapshot.currentSceneIndex = telemetryCurrentScene_.load();
    snapshot.destinationSceneIndex = telemetryDestinationScene_.load();
    snapshot.elapsedFrames = telemetryElapsedFrames_.load();
    snapshot.totalFrames = telemetryTotalFrames_.load();
    snapshot.transitioning = telemetryTransitioning_.load();
    snapshot.currentIdentity = telemetryCurrentIdentity_.load();
    snapshot.destinationIdentity = telemetryDestinationIdentity_.load();
    if (before == telemetrySerial_.load()) return snapshot;
  }
  // Every field remains atomic even when publication overlaps all attempts.
  // A brief missed UI poll is preferable to blocking the audio thread.
  return {};
}

} // namespace ardor
