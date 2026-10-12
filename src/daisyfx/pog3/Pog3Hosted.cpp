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
void Pog3Hosted::fail(float* left, float* right, std::size_t frames) noexcept {
  if (!failed_) { failed_ = true; misses_.fetch_add(1, std::memory_order_relaxed); }
  std::fill_n(left, frames, 0.0f); std::fill_n(right, frames, 0.0f);
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
  const auto generation = submitted_ + 1;
  const auto expected = generation > delayBlocks_ ? generation - delayBlocks_ : 0;
  if (failed_ || frames != frames_ || executor_.completedGeneration() < expected) {
    fail(outputLeft, outputRight, frames); return;
  }
  snapshots_[generation % snapshots_.size()] = requested_.read();
  ParallelStereoStageProcessResult result;
  if (!executor_.processBlock(left, right, outputLeft, outputRight, frames, result)
      || !result.accepted || result.submittedGeneration != generation) {
    fail(outputLeft, outputRight, frames); return;
  }
  submitted_ = generation;
  if (expected == 0) {
    std::fill_n(outputLeft, frames, 0.0f); std::fill_n(outputRight, frames, 0.0f);
  } else if (!result.outputReady || result.usedFallback || result.outputGeneration != expected) {
    fail(outputLeft, outputRight, frames);
  }
}
void Pog3Hosted::work(void* context, float* left, float* right, std::size_t frames) noexcept {
  auto& host = *static_cast<Pog3Hosted*>(context);
  const auto generation = ++host.workerGeneration_;
  ScopedDenormalGuard guard;
  host.apply(host.snapshots_[generation % host.snapshots_.size()]);
  for (std::size_t i = 0; i < frames; ++i) {
    const auto value = host.core_.process({left[i], right[i]}).mixed;
    left[i] = value.left; right[i] = value.right;
  }
}
void Pog3Hosted::reset() noexcept {
  executor_.reset();
  submitted_ = workerGeneration_ = remaining_ = 0;
  misses_.store(0, std::memory_order_relaxed); failed_ = false;
  apply(requested_.read()); core_.reset();
}
} // namespace ardor::pog3
