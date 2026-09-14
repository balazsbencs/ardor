#include "dsp/WdwRoutingProgram.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ardor {

namespace {

constexpr float kQuarterPi = 0.78539816339744830962f;
constexpr float kMixSmoothing = 0.125f;

} // namespace

void WdwRoutingProgram::DelayLine::prepare(std::size_t delayFramesValue,
                                             std::size_t blockSize)
{
  delayFrames = delayFramesValue;
  writeIndex = 0;
  if (delayFrames == 0) {
    storage.clear();
    return;
  }
  storage.assign(delayFrames + blockSize + 1, 0.0f);
}

void WdwRoutingProgram::DelayLine::process(const float* input, float* output,
                                            std::size_t frames) noexcept
{
  if (!input || !output || frames == 0) return;
  if (delayFrames == 0) {
    std::copy(input, input + frames, output);
    return;
  }

  const std::size_t capacity = storage.size();
  if (capacity == 0) {
    std::fill(output, output + frames, 0.0f);
    return;
  }
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const std::size_t readIndex =
      (writeIndex + capacity - (delayFrames % capacity)) % capacity;
    output[frame] = storage[readIndex];
    storage[writeIndex] = input[frame];
    writeIndex = (writeIndex + 1) % capacity;
  }
}

void WdwRoutingProgram::DelayLine::reset() noexcept
{
  std::fill(storage.begin(), storage.end(), 0.0f);
  writeIndex = 0;
}

float WdwRoutingProgram::smooth(float current, float target) noexcept
{
  if (!std::isfinite(target)) return current;
  return current + (target - current) * kMixSmoothing;
}

bool WdwRoutingProgram::normalizeMix(WdwMixConfig config, float& dryLeft,
                                     float& dryRight, float& wetLevel,
                                     float& wetWidth) noexcept
{
  if (!std::isfinite(config.dryLevel) || !std::isfinite(config.dryPan)
      || !std::isfinite(config.wetLevel) || !std::isfinite(config.wetWidth)) {
    return false;
  }

  const float dryLevel = std::max(config.dryLevel, 0.0f);
  const float dryPan = std::clamp(config.dryPan, -1.0f, 1.0f);
  const float angle = (dryPan + 1.0f) * kQuarterPi;
  dryLeft = config.dryEnabled ? dryLevel * std::cos(angle) : 0.0f;
  dryRight = config.dryEnabled ? dryLevel * std::sin(angle) : 0.0f;
  wetLevel = config.wetEnabled ? std::max(config.wetLevel, 0.0f) : 0.0f;
  wetWidth = std::clamp(config.wetWidth, 0.0f, 1.0f);
  return true;
}

bool WdwRoutingProgram::prepare(WdwRoutingLane dry, WdwRoutingLane wet,
                                WdwRoutingProgramOptions options,
                                std::string& error)
{
  error.clear();
  if (prepared_) {
    error = "WDW routing program cannot be reconfigured after prepare";
    return false;
  }
  if (dry.id.empty() || wet.id.empty() || dry.id == wet.id) {
    error = "WDW routing program requires two distinct lane IDs";
    return false;
  }
  if (!dry.chain || !wet.chain) {
    error = "WDW routing program requires both RuntimeChain instances";
    return false;
  }
  if (options.executor.blockSize == 0
      || !std::isfinite(options.executor.sampleRate)
      || options.executor.sampleRate <= 0.0) {
    error = "WDW routing program requires a valid block size and sample rate";
    return false;
  }
  if (options.executor.mode == WdwPairExecutionMode::Pipelined) {
    if (dry.workerCpu >= 0 && wet.workerCpu >= 0
        && dry.workerCpu == wet.workerCpu) {
      error = "WDW routing program requires distinct lane worker CPUs";
      return false;
    }
    if (options.audioCpu >= 0
        && (dry.workerCpu == options.audioCpu || wet.workerCpu == options.audioCpu)) {
      error = "WDW routing worker CPU cannot equal the audio CPU";
      return false;
    }
  }

  float dryLeftGain = 0.0f;
  float dryRightGain = 0.0f;
  float wetLevel = 0.0f;
  float wetWidth = 0.0f;
  if (!normalizeMix(options.mix, dryLeftGain, dryRightGain, wetLevel, wetWidth)) {
    error = "WDW routing program received non-finite mix parameters";
    return false;
  }

  const std::size_t maximumLatency =
    std::max(options.dryLatencyFrames, options.wetLatencyFrames);
  const auto maximumSize = std::numeric_limits<std::size_t>::max();
  if (options.executor.blockSize > maximumSize - 1
      || maximumLatency > maximumSize - options.executor.blockSize - 1) {
    error = "WDW routing latency exceeds addressable buffer size";
    return false;
  }

  dryContext_ = std::make_unique<LaneContext>();
  wetContext_ = std::make_unique<LaneContext>();
  dryContext_->chain = std::move(dry.chain);
  wetContext_->chain = std::move(wet.chain);
  dryContext_->monoOutput = true;
  wetContext_->monoOutput = false;
  dryContext_->chain->prepareBlockSize(options.executor.blockSize);
  wetContext_->chain->prepareBlockSize(options.executor.blockSize);

  blockSize_ = options.executor.blockSize;
  sampleRate_ = options.executor.sampleRate;
  dryAlignmentFrames_ = maximumLatency - options.dryLatencyFrames;
  wetAlignmentFrames_ = maximumLatency - options.wetLatencyFrames;
  latencyFrames_ = maximumLatency
    + (options.executor.mode == WdwPairExecutionMode::Pipelined ? blockSize_ : 0);

  dryPairLeft_.assign(blockSize_, 0.0f);
  dryPairRight_.assign(blockSize_, 0.0f);
  wetPairLeft_.assign(blockSize_, 0.0f);
  wetPairRight_.assign(blockSize_, 0.0f);
  alignedDryLeft_.assign(blockSize_, 0.0f);
  alignedDryRight_.assign(blockSize_, 0.0f);
  alignedWetLeft_.assign(blockSize_, 0.0f);
  alignedWetRight_.assign(blockSize_, 0.0f);
  dryDelayLeft_.prepare(dryAlignmentFrames_, blockSize_);
  dryDelayRight_.prepare(dryAlignmentFrames_, blockSize_);
  wetDelayLeft_.prepare(wetAlignmentFrames_, blockSize_);
  wetDelayRight_.prepare(wetAlignmentFrames_, blockSize_);

  mixTargets_.dryLeftGain.store(dryLeftGain, std::memory_order_relaxed);
  mixTargets_.dryRightGain.store(dryRightGain, std::memory_order_relaxed);
  mixTargets_.wetLevel.store(wetLevel, std::memory_order_relaxed);
  mixTargets_.wetWidth.store(wetWidth, std::memory_order_relaxed);
  mixCurrent_ = {dryLeftGain, dryRightGain, wetLevel, wetWidth};
  nonFiniteBlocks_.store(0, std::memory_order_relaxed);

  if (!executor_.configure({dryContext_.get(), &WdwRoutingProgram::processDry,
                            dry.workerCpu},
                           {wetContext_.get(), &WdwRoutingProgram::processWet,
                            wet.workerCpu},
                           options.executor, error)) {
    executor_.clear();
    dryContext_.reset();
    wetContext_.reset();
    return false;
  }

  prepared_ = true;
  return true;
}

bool WdwRoutingProgram::processBlock(const float* input, float* outputLeft,
                                     float* outputRight, std::size_t frames,
                                     WdwRoutingProcessResult& result) noexcept
{
  result = {};
  if (!prepared_ || !input || !outputLeft || !outputRight
      || frames != blockSize_) {
    return false;
  }

  WdwPairProcessResult pairResult;
  if (!executor_.processBlock(input, dryPairLeft_.data(), dryPairRight_.data(),
                             wetPairLeft_.data(), wetPairRight_.data(), frames,
                             pairResult)) {
    return false;
  }

  dryDelayLeft_.process(dryPairLeft_.data(), alignedDryLeft_.data(), frames);
  dryDelayRight_.process(dryPairRight_.data(), alignedDryRight_.data(), frames);
  wetDelayLeft_.process(wetPairLeft_.data(), alignedWetLeft_.data(), frames);
  wetDelayRight_.process(wetPairRight_.data(), alignedWetRight_.data(), frames);

  mixCurrent_.dryLeftGain = smooth(
    mixCurrent_.dryLeftGain,
    mixTargets_.dryLeftGain.load(std::memory_order_acquire));
  mixCurrent_.dryRightGain = smooth(
    mixCurrent_.dryRightGain,
    mixTargets_.dryRightGain.load(std::memory_order_acquire));
  mixCurrent_.wetLevel = smooth(
    mixCurrent_.wetLevel,
    mixTargets_.wetLevel.load(std::memory_order_acquire));
  mixCurrent_.wetWidth = smooth(
    mixCurrent_.wetWidth,
    mixTargets_.wetWidth.load(std::memory_order_acquire));

  bool nonFinite = false;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float dryMono = (alignedDryLeft_[frame] + alignedDryRight_[frame]) * 0.5f;
    const float wetMid = (alignedWetLeft_[frame] + alignedWetRight_[frame]) * 0.5f;
    const float wetSide = (alignedWetLeft_[frame] - alignedWetRight_[frame])
      * 0.5f * mixCurrent_.wetWidth;
    outputLeft[frame] = dryMono * mixCurrent_.dryLeftGain
      + (wetMid + wetSide) * mixCurrent_.wetLevel;
    outputRight[frame] = dryMono * mixCurrent_.dryRightGain
      + (wetMid - wetSide) * mixCurrent_.wetLevel;
    if (!std::isfinite(outputLeft[frame]) || !std::isfinite(outputRight[frame])) {
      outputLeft[frame] = 0.0f;
      outputRight[frame] = 0.0f;
      nonFinite = true;
    }
  }
  if (nonFinite) nonFiniteBlocks_.fetch_add(1, std::memory_order_relaxed);

  result.accepted = pairResult.accepted;
  result.outputReady = pairResult.outputReady;
  result.pairReady = pairResult.pairReady;
  result.workersReady = pairResult.workersReady;
  result.usedFallback = pairResult.usedFallback;
  result.heldLastPair = pairResult.heldLastPair;
  result.submittedGeneration = pairResult.submittedGeneration;
  result.outputGeneration = pairResult.outputGeneration;
  result.outputAgeBlocks = pairResult.outputAgeBlocks;
  return true;
}

bool WdwRoutingProgram::processSample(float input, float& outputLeft,
                                      float& outputRight) noexcept
{
  if (!prepared_ || executor_.parallelEnabled() || !std::isfinite(input)) {
    outputLeft = 0.0f;
    outputRight = 0.0f;
    return false;
  }

  const StereoSample dry = dryContext_ && dryContext_->chain
    ? dryContext_->chain->process({input, input}) : StereoSample{};
  const StereoSample wet = wetContext_ && wetContext_->chain
    ? wetContext_->chain->process({input, input}) : StereoSample{};

  float dryLeft = 0.0f;
  float dryRight = 0.0f;
  float wetLeft = 0.0f;
  float wetRight = 0.0f;
  dryDelayLeft_.process(&dry.left, &dryLeft, 1);
  dryDelayRight_.process(&dry.right, &dryRight, 1);
  wetDelayLeft_.process(&wet.left, &wetLeft, 1);
  wetDelayRight_.process(&wet.right, &wetRight, 1);

  mixCurrent_.dryLeftGain = smooth(
    mixCurrent_.dryLeftGain,
    mixTargets_.dryLeftGain.load(std::memory_order_acquire));
  mixCurrent_.dryRightGain = smooth(
    mixCurrent_.dryRightGain,
    mixTargets_.dryRightGain.load(std::memory_order_acquire));
  mixCurrent_.wetLevel = smooth(
    mixCurrent_.wetLevel,
    mixTargets_.wetLevel.load(std::memory_order_acquire));
  mixCurrent_.wetWidth = smooth(
    mixCurrent_.wetWidth,
    mixTargets_.wetWidth.load(std::memory_order_acquire));

  const float dryMono = (dryLeft + dryRight) * 0.5f;
  const float wetMid = (wetLeft + wetRight) * 0.5f;
  const float wetSide = (wetLeft - wetRight) * 0.5f * mixCurrent_.wetWidth;
  outputLeft = dryMono * mixCurrent_.dryLeftGain
    + (wetMid + wetSide) * mixCurrent_.wetLevel;
  outputRight = dryMono * mixCurrent_.dryRightGain
    + (wetMid - wetSide) * mixCurrent_.wetLevel;
  if (!std::isfinite(outputLeft) || !std::isfinite(outputRight)) {
    outputLeft = 0.0f;
    outputRight = 0.0f;
    nonFiniteBlocks_.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}

void WdwRoutingProgram::reset() noexcept
{
  if (!prepared_) return;
  executor_.reset();
  if (dryContext_ && dryContext_->chain) dryContext_->chain->reset();
  if (wetContext_ && wetContext_->chain) wetContext_->chain->reset();
  dryDelayLeft_.reset();
  dryDelayRight_.reset();
  wetDelayLeft_.reset();
  wetDelayRight_.reset();
  std::fill(dryPairLeft_.begin(), dryPairLeft_.end(), 0.0f);
  std::fill(dryPairRight_.begin(), dryPairRight_.end(), 0.0f);
  std::fill(wetPairLeft_.begin(), wetPairLeft_.end(), 0.0f);
  std::fill(wetPairRight_.begin(), wetPairRight_.end(), 0.0f);
  std::fill(alignedDryLeft_.begin(), alignedDryLeft_.end(), 0.0f);
  std::fill(alignedDryRight_.begin(), alignedDryRight_.end(), 0.0f);
  std::fill(alignedWetLeft_.begin(), alignedWetLeft_.end(), 0.0f);
  std::fill(alignedWetRight_.begin(), alignedWetRight_.end(), 0.0f);
  mixCurrent_.dryLeftGain = mixTargets_.dryLeftGain.load(std::memory_order_acquire);
  mixCurrent_.dryRightGain = mixTargets_.dryRightGain.load(std::memory_order_acquire);
  mixCurrent_.wetLevel = mixTargets_.wetLevel.load(std::memory_order_acquire);
  mixCurrent_.wetWidth = mixTargets_.wetWidth.load(std::memory_order_acquire);
  nonFiniteBlocks_.store(0, std::memory_order_relaxed);
}

bool WdwRoutingProgram::setMix(WdwMixConfig config) noexcept
{
  if (!prepared_) return false;
  float dryLeftGain = 0.0f;
  float dryRightGain = 0.0f;
  float wetLevel = 0.0f;
  float wetWidth = 0.0f;
  if (!normalizeMix(config, dryLeftGain, dryRightGain, wetLevel, wetWidth)) {
    return false;
  }
  mixTargets_.dryLeftGain.store(dryLeftGain, std::memory_order_release);
  mixTargets_.dryRightGain.store(dryRightGain, std::memory_order_release);
  mixTargets_.wetLevel.store(wetLevel, std::memory_order_release);
  mixTargets_.wetWidth.store(wetWidth, std::memory_order_release);
  return true;
}

bool WdwRoutingProgram::setParametricEqBand(const std::string& id, std::size_t band,
                                            const EqBandParams& params)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setParametricEqBand(id, band, params))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setParametricEqBand(id, band, params));
}

bool WdwRoutingProgram::setParametricEqPassFilter(const std::string& id, EqPassFilterKind kind,
                                                  const EqPassFilterParams& params)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setParametricEqPassFilter(id, kind, params))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setParametricEqPassFilter(id, kind, params));
}

bool WdwRoutingProgram::setDaisyParameter(const std::string& id, const std::string& key,
                                          float normalized)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setDaisyParameter(id, key, normalized))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setDaisyParameter(id, key, normalized));
}

bool WdwRoutingProgram::setCabParameter(const std::string& id, const std::string& key, float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setCabParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setCabParameter(id, key, value));
}

bool WdwRoutingProgram::setCompressorParameter(const std::string& id, const std::string& key,
                                               float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setCompressorParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setCompressorParameter(id, key, value));
}

bool WdwRoutingProgram::compressorGainReductionDb(const std::string& id, float& outDb) const
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->compressorGainReductionDb(id, outDb))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->compressorGainReductionDb(id, outDb));
}

bool WdwRoutingProgram::setNoiseGateParameter(const std::string& id, const std::string& key,
                                              float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setNoiseGateParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setNoiseGateParameter(id, key, value));
}

bool WdwRoutingProgram::setTransientShaperParameter(const std::string& id, const std::string& key,
                                                    float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setTransientShaperParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setTransientShaperParameter(id, key, value));
}

bool WdwRoutingProgram::setWahParameter(const std::string& id, const std::string& key, float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setWahParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setWahParameter(id, key, value));
}

bool WdwRoutingProgram::setDistortionParameter(const std::string& id, const std::string& key,
                                               float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setDistortionParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setDistortionParameter(id, key, value));
}

bool WdwRoutingProgram::setStereoWidenerParameter(const std::string& id, const std::string& key,
                                                  float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setStereoWidenerParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setStereoWidenerParameter(id, key, value));
}

bool WdwRoutingProgram::setIrReverbParameter(const std::string& id, const std::string& key,
                                             float value)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setIrReverbParameter(id, key, value))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setIrReverbParameter(id, key, value));
}

bool WdwRoutingProgram::setBlockEnabled(const std::string& id, bool enabled)
{
  return (dryContext_ && dryContext_->chain
          && dryContext_->chain->setBlockEnabled(id, enabled))
    || (wetContext_ && wetContext_->chain
        && wetContext_->chain->setBlockEnabled(id, enabled));
}

void WdwRoutingProgram::processDry(void* opaque, const float* input, float* left,
                                   float* right, std::size_t frames) noexcept
{
  auto* context = static_cast<LaneContext*>(opaque);
  if (!context || !context->chain || !input || !left || !right) {
    if (left) std::fill(left, left + frames, 0.0f);
    if (right) std::fill(right, right + frames, 0.0f);
    return;
  }
  context->chain->processBlock(input, left, right, frames);
  if (!context->monoOutput) return;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float mono = (left[frame] + right[frame]) * 0.5f;
    left[frame] = mono;
    right[frame] = mono;
  }
}

void WdwRoutingProgram::processWet(void* opaque, const float* input, float* left,
                                   float* right, std::size_t frames) noexcept
{
  auto* context = static_cast<LaneContext*>(opaque);
  if (!context || !context->chain || !input || !left || !right) {
    if (left) std::fill(left, left + frames, 0.0f);
    if (right) std::fill(right, right + frames, 0.0f);
    return;
  }
  context->chain->processBlock(input, left, right, frames);
}

std::size_t WdwRoutingProgram::alignmentDelayFrames(std::size_t lane) const noexcept
{
  if (lane == 0) return dryAlignmentFrames_;
  if (lane == 1) return wetAlignmentFrames_;
  return 0;
}

std::string WdwRoutingProgram::firstNonFiniteBlockId() const
{
  if (dryContext_ && dryContext_->chain) {
    const std::string id = dryContext_->chain->firstNonFiniteBlockId();
    if (!id.empty()) return "dry/" + id;
  }
  if (wetContext_ && wetContext_->chain) {
    const std::string id = wetContext_->chain->firstNonFiniteBlockId();
    if (!id.empty()) return "wet/" + id;
  }
  return {};
}

ClipDiagnosticsSnapshot WdwRoutingProgram::takeClipDiagnostics()
{
  ClipDiagnosticsSnapshot diagnostics;
  const auto append = [&diagnostics](RuntimeChain* chain, const char* prefix) {
    if (!chain) return;
    auto stages = chain->takeClipDiagnostics();
    for (auto& stage : stages) {
      stage.id = std::string{prefix} + "/" + stage.id;
      diagnostics.stages.push_back(std::move(stage));
    }
  };
  append(dryContext_ ? dryContext_->chain.get() : nullptr, "dry");
  append(wetContext_ ? wetContext_->chain.get() : nullptr, "wet");
  return diagnostics;
}

std::size_t WdwRoutingProgram::tailFrames() const noexcept
{
  const std::size_t dryTail = dryContext_ && dryContext_->chain
    ? dryContext_->chain->tailFrames() + dryAlignmentFrames_ : dryAlignmentFrames_;
  const std::size_t wetTail = wetContext_ && wetContext_->chain
    ? wetContext_->chain->tailFrames() + wetAlignmentFrames_ : wetAlignmentFrames_;
  return std::max(dryTail, wetTail);
}

} // namespace ardor
