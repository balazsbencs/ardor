#include "daisyfx/pog3/Pog3Hosted.h"
#include "dsp/DenormalGuard.h"
#include <algorithm>
#include <cmath>

namespace ardor::pog3 {
bool Pog3Hosted::configure(const nlohmann::json& params, float sampleRate,
                          std::size_t frames, int workerCpu, bool realtime,
                          std::string& error) {
  executor_.clear();
  parallel_ = failed_ = false;
  if (!core_.configure(params, sampleRate, error)) return false;
  requested_.store(core_.targetValues());
  if (workerCpu >= 0) {
    if (frames != 32 && frames != 64 && frames != 128) {
      error = "Poly Octave 3 requires a 32, 64 or 128 frame audio block";
      return false;
    }
    frames_ = frames;
    delayBlocks_ = 256 / frames;
    snapshots_.assign(delayBlocks_, requested_.read());
    ParallelStereoStageExecutorOptions options;
    options.blockSize = frames;
    options.sampleRate = sampleRate;
    options.mode = ParallelStereoStageExecutionMode::Pipelined;
    options.pipelineSlots = std::max<std::size_t>(2, delayBlocks_);
    options.fixedOutputDelayBlocks = delayBlocks_;
    options.workerCpu = workerCpu;
    options.workerPriority = 69;
    options.requireRealtimeScheduling = realtime;
    options.requireAffinity = true;
    if (!executor_.configure(work, this, options, error)) return false;
    parallel_ = true;
  }
  reset();
  return true;
}
bool Pog3Hosted::setTarget(std::string_view key, float value) noexcept {
  const auto parameter = findParameter(key);
  return parameter && setTarget(index(*parameter), value);
}
bool Pog3Hosted::setTarget(std::size_t parameter, float value) noexcept {
  if (!std::isfinite(value)) return false;
  if (!kExperimentalFreezeEnabled && parameter == index(Parameter::ExpressionMode)
      && choiceIndex(value, 7) >= 5) return false;
  return requested_.setTarget(parameter, value);
}
void Pog3Hosted::apply(const Values& values) noexcept {
  for (std::size_t i = 0; i < values.size(); ++i) core_.setParameterTarget(i, values[i]);
}
VoiceStageOutput Pog3Hosted::process(PitchStereo input) noexcept {
  // A worker is block-only. In particular, never silently process its state
  // on the caller as a sequential fallback.
  if (parallel_) return {};
  if (remaining_ == 0) {
    apply(requested_.read()); remaining_ = Pog3Processor::kControlPeriod;
  }
  --remaining_;
  return core_.process(input);
}
void Pog3Hosted::fail(bool retryAllowed) noexcept {
  if (failed_) return;
  failed_ = true;
  misses_.fetch_add(1, std::memory_order_relaxed);
  bypassFade_ = 0; holdLeft_ = lastLeft_; holdRight_ = lastRight_;
  if (!retryAllowed) retryUsed_ = true;
  recoveryState_.store(retryUsed_ ? RecoveryState::Bypassed : RecoveryState::Waiting,
                       std::memory_order_release);
}
void Pog3Hosted::rememberDry(const float* left, const float* right, std::size_t frames) noexcept {
  for (std::size_t i = 0; i < frames; ++i) {
    dryBlockLeft_[i] = dryLeft_[dryCursor_]; dryBlockRight_[i] = dryRight_[dryCursor_];
    dryLeft_[dryCursor_] = left[i]; dryRight_[dryCursor_] = right[i];
    dryCursor_ = (dryCursor_ + 1) % kDelaySamples;
  }
}
void Pog3Hosted::rememberOutput(const float* left, const float* right, std::size_t frames) noexcept {
  lastLeft_ = left[frames - 1]; lastRight_ = right[frames - 1];
}
void Pog3Hosted::fallback(float* left, float* right, std::size_t frames) noexcept {
  for (std::size_t i = 0; i < frames; ++i) {
    const float gain = static_cast<float>(std::min(++bypassFade_, kBypassFadeSamples))
      / kBypassFadeSamples;
    left[i] = holdLeft_ + gain * (dryBlockLeft_[i] - holdLeft_);
    right[i] = holdRight_ + gain * (dryBlockRight_[i] - holdRight_);
    // Saturate the counter even if a permanent bypass runs for years.
    bypassFade_ = std::min(bypassFade_, kBypassFadeSamples);
  }
  rememberOutput(left, right, frames);
}
void Pog3Hosted::processBlock(const float* left, const float* right,
                             float* outputLeft, float* outputRight,
                             std::size_t frames) noexcept {
  if (!parallel_) {
    ScopedDenormalGuard guard;
    for (std::size_t i = 0; i < frames; ++i) {
      const auto value = process({left[i], right[i]}).mixed;
      outputLeft[i] = value.left; outputRight[i] = value.right;
    }
    return;
  }
  if (frames != frames_) {
    fail(false); // A broken host quantum cannot be fixed by restarting DSP.
    std::fill_n(outputLeft, frames, 0.0f); std::fill_n(outputRight, frames, 0.0f);
    return;
  }
  rememberDry(left, right, frames);
  if (failed_) {
    if (retryUsed_ || executor_.completedGeneration() < submitted_) {
      fallback(outputLeft, outputRight, frames); return;
    }
    // Drain by observation, never by waiting in the callback. Restart the core
    // on its worker at the next dated input, preserving all other DSP/looper/
    // scene state and the latest published controls. One retry per reset.
    retryUsed_ = true; failed_ = false; restartGeneration_ = submitted_ + 1;
    recoveryFade_ = 0;
    recoveryState_.store(RecoveryState::Restarting, std::memory_order_release);
  }
  const auto generation = submitted_ + 1;
  const auto expected = generation > delayBlocks_ ? generation - delayBlocks_ : 0;
  if (executor_.completedGeneration() < expected) {
    fail(true); fallback(outputLeft, outputRight, frames); return;
  }
  snapshots_[generation % snapshots_.size()] = requested_.read();
  ParallelStereoStageProcessResult result;
  if (!executor_.processBlock(left, right, outputLeft, outputRight, frames, result)
      || !result.accepted || result.submittedGeneration != generation) {
    fail(true); fallback(outputLeft, outputRight, frames); return;
  }
  submitted_ = generation;
  if (expected < restartGeneration_ && retryUsed_) {
    // Discard pre-fault publications during the fresh pipeline's startup.
    fallback(outputLeft, outputRight, frames); return;
  } else if (expected == 0) {
    std::fill_n(outputLeft, frames, 0.0f); std::fill_n(outputRight, frames, 0.0f);
  } else if (!result.outputReady || result.usedFallback || result.outputGeneration != expected) {
    fail(true); fallback(outputLeft, outputRight, frames); return;
  }
  if (recoveryState_.load(std::memory_order_relaxed) == RecoveryState::Restarting) {
    for (std::size_t i = 0; i < frames; ++i) {
      const float gain = static_cast<float>(std::min(++recoveryFade_, kRecoveryFadeSamples))
        / kRecoveryFadeSamples;
      outputLeft[i] = dryBlockLeft_[i] + gain * (outputLeft[i] - dryBlockLeft_[i]);
      outputRight[i] = dryBlockRight_[i] + gain * (outputRight[i] - dryBlockRight_[i]);
      recoveryFade_ = std::min(recoveryFade_, kRecoveryFadeSamples);
    }
    if (recoveryFade_ == kRecoveryFadeSamples) {
      recoveries_.fetch_add(1, std::memory_order_relaxed);
      recoveryState_.store(RecoveryState::Healthy, std::memory_order_release);
    }
  }
  rememberOutput(outputLeft, outputRight, frames);
}
void Pog3Hosted::work(void* context, float* left, float* right, std::size_t frames) noexcept {
  auto& host = *static_cast<Pog3Hosted*>(context);
  const auto generation = ++host.workerGeneration_;
  ScopedDenormalGuard guard;
  host.apply(host.snapshots_[generation % host.snapshots_.size()]);
  if (generation == host.restartGeneration_ && generation > 1) host.core_.reset();
  for (std::size_t i = 0; i < frames; ++i) {
    const auto value = host.core_.process({left[i], right[i]}).mixed;
    left[i] = value.left; right[i] = value.right;
  }
}
void Pog3Hosted::reset() noexcept {
  executor_.reset();
  submitted_ = workerGeneration_ = remaining_ = 0;
  restartGeneration_ = 1; retryUsed_ = false;
  misses_.store(0, std::memory_order_relaxed); failed_ = false;
  recoveries_.store(0, std::memory_order_relaxed);
  recoveryState_.store(RecoveryState::Healthy, std::memory_order_relaxed);
  dryLeft_.fill(0); dryRight_.fill(0); dryBlockLeft_.fill(0); dryBlockRight_.fill(0);
  dryCursor_ = bypassFade_ = recoveryFade_ = 0;
  lastLeft_ = lastRight_ = holdLeft_ = holdRight_ = 0;
  apply(requested_.read()); core_.reset();
}
} // namespace ardor::pog3
