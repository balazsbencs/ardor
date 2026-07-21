#include "PedalEngine.h"

#include "DenormalGuard.h"

#include "daisyfx/DaisyFxProcessor.h"
#include "dynamics/CompressorProcessor.h"
#include "equalizer/EqParameters.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ardor {

namespace {

void commitLevel(std::atomic<uint32_t>& peakBits, std::atomic<uint64_t>& overloadFrames,
                 float peak, uint64_t overloadCount)
{
  const uint32_t bits = std::bit_cast<uint32_t>(peak);
  uint32_t previous = peakBits.load(std::memory_order_relaxed);
  while (previous < bits
         && !peakBits.compare_exchange_weak(previous, bits, std::memory_order_relaxed)) {
  }
  if (overloadCount > 0) {
    overloadFrames.fetch_add(overloadCount, std::memory_order_relaxed);
  }
}

void observeLevel(std::atomic<uint32_t>& peakBits, std::atomic<uint64_t>& overloadFrames,
                  float left, float right)
{
  const float peak = std::max(std::fabs(left), std::fabs(right));
  commitLevel(peakBits, overloadFrames, peak, peak > 1.0f ? 1U : 0U);
}

ClipStageSnapshot takeLevel(std::atomic<uint32_t>& peakBits,
                            std::atomic<uint64_t>& overloadFrames,
                            SignalStageKind kind, std::string id)
{
  return {
    kind,
    std::move(id),
    std::bit_cast<float>(peakBits.exchange(0, std::memory_order_relaxed)),
    overloadFrames.exchange(0, std::memory_order_relaxed),
  };
}

std::string stageLabel(const ClipStageSnapshot& stage)
{
  std::string label = signalStageKindName(stage.kind);
  if (!stage.id.empty()) {
    label += ":" + stage.id;
  }
  return label;
}

} // namespace

const char* signalStageKindName(SignalStageKind kind) noexcept
{
  switch (kind) {
  case SignalStageKind::Input: return "input";
  case SignalStageKind::Nam: return "nam";
  case SignalStageKind::Cab: return "ir";
  case SignalStageKind::Daisy: return "daisy";
  case SignalStageKind::Compressor: return "compressor";
  case SignalStageKind::Equalizer: return "eq";
  case SignalStageKind::Output: return "output";
  }
  return "unknown";
}

std::string formatClipDiagnostics(const ClipDiagnosticsSnapshot& diagnostics)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << "levels";
  std::string firstOverload;
  for (const auto& stage : diagnostics.stages) {
    const std::string label = stageLabel(stage);
    const float db = stage.peak > 0.0f ? 20.0f * std::log10(stage.peak) : -120.0f;
    out << ' ' << label << '=' << db << "dBFS";
    if (stage.overloadFrames > 0) {
      out << " CLIP[" << stage.overloadFrames << ']';
      if (firstOverload.empty()) {
        firstOverload = label;
      }
    }
  }
  if (diagnostics.limiterFrames > 0) {
    out << " limiter=[" << diagnostics.limiterFrames << ']';
  }
  if (!firstOverload.empty()) {
    out << " first=" << firstOverload;
  }
  return out.str();
}

void PedalEngine::setSampleRate(double sampleRate)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
    return;
  }
  sampleRate_ = static_cast<float>(sampleRate);
  constexpr float kGainSmoothingSeconds = 0.005f;
  gainSmoothingCoefficient_ = 1.0f - std::exp(-1.0f / (kGainSmoothingSeconds * sampleRate_));
}

bool PedalEngine::loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
                          std::string id, float slimmableSize)
{
  setSampleRate(sampleRate);
  prepareBlockSize(static_cast<size_t>(std::max(1, maxBlockSize)));
  return chain_.addNam(modelPath, sampleRate, maxBlockSize, std::move(id), slimmableSize);
}

void PedalEngine::loadIr(std::vector<float> impulse)
{
  addCab(std::move(impulse), cabLevel_.load(std::memory_order_relaxed), cabMix_.load(std::memory_order_relaxed));
}

void PedalEngine::addCab(std::vector<float> impulse, float level, float mix, std::string id)
{
  const float safeLevel = std::isfinite(level) ? std::max(0.0f, level) : 1.0f;
  const float safeMix = std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 1.0f;
  cabLevel_.store(safeLevel, std::memory_order_relaxed);
  cabMix_.store(safeMix, std::memory_order_relaxed);
  chain_.addCab(std::move(impulse), safeLevel, safeMix, std::move(id));
}

bool PedalEngine::addDaisyFx(std::string id, const std::string& blockType, const nlohmann::json& params,
                             float sampleRate, std::string& error)
{
  DaisyFxProcessor processor;
  if (!processor.configure(blockType, params, sampleRate, error)) {
    return false;
  }
  chain_.addDaisy(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setDaisyParameter(const std::string& id, const std::string& key, float normalized)
{
  return chain_.setDaisyParameter(id, key, normalized);
}

bool PedalEngine::addCompressor(std::string id, const nlohmann::json& params, float sampleRate, std::string& error)
{
  CompressorProcessor processor;
  if (!processor.configure(params, sampleRate, error)) {
    return false;
  }
  chain_.addCompressor(std::move(id), std::move(processor));
  return true;
}

bool PedalEngine::setCompressorParameter(const std::string& id, const std::string& key, float value)
{
  return chain_.setCompressorParameter(id, key, value);
}

bool PedalEngine::addParametricEq(const std::string& id, const nlohmann::json& params,
                                  float sampleRate, std::string& error)
{
  return chain_.addParametricEq(id, parametricEqParamsFromJson(params), sampleRate, error);
}

bool PedalEngine::setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params)
{
  return chain_.setParametricEqBand(id, band, params);
}

void PedalEngine::prepareBlockSize(size_t frames)
{
  if (frames == 0 || blockSize_ == frames) {
    return;
  }
  blockSize_ = frames;
  sanitizedInput_.assign(frames, 0.0f);
  gainedInput_.assign(frames, 0.0f);
  cabLevelBlock_.assign(frames, 1.0f);
  cabMixBlock_.assign(frames, 1.0f);
  chain_.prepareBlockSize(frames);
}

void PedalEngine::clearEffects()
{
  chain_.clear();
}

void PedalEngine::setInputGain(float gain)
{
  inputGain_.store(std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f, std::memory_order_relaxed);
}

void PedalEngine::setOutputGain(float gain)
{
  outputGain_.store(std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f, std::memory_order_relaxed);
}

void PedalEngine::setMasterVolume(float gain)
{
  masterVolume_.store(std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f, std::memory_order_relaxed);
}

void PedalEngine::setEffectsBypassed(bool bypassed)
{
  // This setter is used by the control thread while process/processBlock run
  // on the audio thread. Resetting NAM, convolution, or time effects from the
  // callback is unbounded work, so bypass is deliberately state-preserving.
  effectsBypassed_.store(bypassed, std::memory_order_relaxed);
}

void PedalEngine::setSafetyLimit(float limit)
{
  safetyLimit_.store(std::isfinite(limit) ? std::max(0.0f, limit) : 0.8912509f, std::memory_order_relaxed);
}

void PedalEngine::setSafetyLimiterEnabled(bool enabled)
{
  safetyLimiterEnabled_.store(enabled, std::memory_order_relaxed);
}

void PedalEngine::setCabLevel(float gain)
{
  const float level = std::isfinite(gain) ? std::max(0.0f, gain) : 1.0f;
  cabLevel_.store(level, std::memory_order_relaxed);
}

void PedalEngine::setCabMix(float mix)
{
  const float cabMix = std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 1.0f;
  cabMix_.store(cabMix, std::memory_order_relaxed);
}

uint64_t PedalEngine::nonFiniteInputSamples() const noexcept
{
  return nonFiniteInputSamples_.load(std::memory_order_relaxed);
}

uint64_t PedalEngine::blockSizeMismatchCount() const noexcept
{
  return blockSizeMismatchCount_.load(std::memory_order_relaxed);
}

uint64_t PedalEngine::nonFiniteBlockCount() const noexcept
{
  return chain_.nonFiniteBlockCount();
}

std::string PedalEngine::firstNonFiniteBlockId() const
{
  return chain_.firstNonFiniteBlockId();
}

ClipDiagnosticsSnapshot PedalEngine::takeClipDiagnostics()
{
  ClipDiagnosticsSnapshot diagnostics;
  auto chainDiagnostics = chain_.takeClipDiagnostics();
  diagnostics.stages.reserve(chainDiagnostics.size() + 2);
  diagnostics.stages.push_back(takeLevel(inputPeakBits_, inputOverloadFrames_,
                                         SignalStageKind::Input, {}));
  for (auto& stage : chainDiagnostics) {
    diagnostics.stages.push_back(std::move(stage));
  }
  diagnostics.stages.push_back(takeLevel(outputPeakBits_, outputOverloadFrames_,
                                         SignalStageKind::Output, {}));
  diagnostics.limiterFrames = limiterFrames_.exchange(0, std::memory_order_relaxed);
  return diagnostics;
}

void PedalEngine::replacePreparedProgram(PedalEngine&& prepared)
{
  chain_ = std::move(prepared.chain_);
  blockSize_ = prepared.blockSize_;
  sanitizedInput_ = std::move(prepared.sanitizedInput_);
  gainedInput_ = std::move(prepared.gainedInput_);
  cabLevelBlock_ = std::move(prepared.cabLevelBlock_);
  cabMixBlock_ = std::move(prepared.cabMixBlock_);
  sampleRate_ = prepared.sampleRate_;
  gainSmoothingCoefficient_ = prepared.gainSmoothingCoefficient_;

  inputGain_.store(prepared.inputGain_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  outputGain_.store(prepared.outputGain_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  safetyLimit_.store(prepared.safetyLimit_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  cabLevel_.store(prepared.cabLevel_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  cabMix_.store(prepared.cabMix_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  safetyLimiterEnabled_.store(prepared.safetyLimiterEnabled_.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);

  inputPeakBits_.store(0, std::memory_order_relaxed);
  inputOverloadFrames_.store(0, std::memory_order_relaxed);
  outputPeakBits_.store(0, std::memory_order_relaxed);
  outputOverloadFrames_.store(0, std::memory_order_relaxed);
  limiterFrames_.store(0, std::memory_order_relaxed);

  // Master volume and bypass belong to the live host rather than a preset.
  // Keep their targets and current ramp positions so a successful swap cannot
  // unexpectedly change hardware volume or introduce a gain step.
}

void PedalEngine::reset()
{
  chain_.reset();
}

size_t PedalEngine::tailFrames() const noexcept
{
  return chain_.tailFrames();
}

void PedalEngine::beginAudioProcessing()
{
  if (audioStarted_) {
    return;
  }
  currentInputGain_ = inputGain_.load(std::memory_order_relaxed);
  currentOutputGain_ = outputGain_.load(std::memory_order_relaxed);
  currentMasterVolume_ = masterVolume_.load(std::memory_order_relaxed);
  currentCabLevel_ = cabLevel_.load(std::memory_order_relaxed);
  currentCabMix_ = cabMix_.load(std::memory_order_relaxed);
  currentEffectsMix_ = effectsBypassed_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
  audioStarted_ = true;
}

float PedalEngine::smoothGain(float& current, const std::atomic<float>& target)
{
  const float requested = target.load(std::memory_order_relaxed);
  current += (requested - current) * gainSmoothingCoefficient_;
  return current;
}

float PedalEngine::smoothEffectsMix()
{
  const float target = effectsBypassed_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
  currentEffectsMix_ += (target - currentEffectsMix_) * gainSmoothingCoefficient_;
  if (std::fabs(target - currentEffectsMix_) < 1.0e-4f) {
    currentEffectsMix_ = target;
  }
  return currentEffectsMix_;
}

StereoSample PedalEngine::equalPowerMix(StereoSample dry, StereoSample wet, float wetMix)
{
  const float clampedMix = std::clamp(wetMix, 0.0f, 1.0f);
  const float dryGain = std::sqrt(1.0f - clampedMix);
  const float wetGain = std::sqrt(clampedMix);
  return {dry.left * dryGain + wet.left * wetGain, dry.right * dryGain + wet.right * wetGain};
}

std::pair<float, float> PedalEngine::process(float input)
{
  const ScopedDenormalGuard denormalGuard;
  beginAudioProcessing();
  if (!std::isfinite(input)) {
    nonFiniteInputSamples_.fetch_add(1, std::memory_order_relaxed);
    input = 0.0f;
  }
  const float masterVolume = smoothGain(currentMasterVolume_, masterVolume_);
  const float afterGain = input * smoothGain(currentInputGain_, inputGain_);
  observeLevel(inputPeakBits_, inputOverloadFrames_, afterGain, afterGain);
  const auto wet = chain_.process({afterGain, afterGain}, smoothGain(currentCabLevel_, cabLevel_),
                                  smoothGain(currentCabMix_, cabMix_));
  const float output = smoothGain(currentOutputGain_, outputGain_) * masterVolume;
  const StereoSample mixed = equalPowerMix({input * masterVolume, input * masterVolume},
                                           {wet.left * output, wet.right * output}, smoothEffectsMix());
  observeLevel(outputPeakBits_, outputOverloadFrames_, mixed.left, mixed.right);
  observeLimiter(mixed.left, mixed.right);
  return {applySafety(mixed.left), applySafety(mixed.right)};
}

void PedalEngine::processBlock(const float* input, float* left, float* right, size_t frames)
{
  const ScopedDenormalGuard denormalGuard;
  beginAudioProcessing();
  if (blockSize_ == 0) {
    prepareBlockSize(frames);
  }
  if (frames != blockSize_) {
    // The realtime adapter owns fixed-quantum assembly. Processing a partial
    // remainder would force a prepared convolver into a different algorithm
    // and corrupt its state, so fail boundedly and make the fault observable.
    blockSizeMismatchCount_.fetch_add(1, std::memory_order_relaxed);
    std::fill(left, left + frames, 0.0f);
    std::fill(right, right + frames, 0.0f);
    return;
  }

  for (size_t i = 0; i < frames; ++i) {
    const float safeInput = std::isfinite(input[i]) ? input[i] : 0.0f;
    if (!std::isfinite(input[i])) {
      nonFiniteInputSamples_.fetch_add(1, std::memory_order_relaxed);
    }
    sanitizedInput_[i] = safeInput;
    gainedInput_[i] = safeInput * smoothGain(currentInputGain_, inputGain_);
    cabLevelBlock_[i] = smoothGain(currentCabLevel_, cabLevel_);
    cabMixBlock_[i] = smoothGain(currentCabMix_, cabMix_);
  }
  float inputPeak = 0.0f;
  uint64_t inputOverloads = 0;
  for (size_t i = 0; i < frames; ++i) {
    const float magnitude = std::fabs(gainedInput_[i]);
    inputPeak = std::max(inputPeak, magnitude);
    inputOverloads += magnitude > 1.0f ? 1U : 0U;
  }
  commitLevel(inputPeakBits_, inputOverloadFrames_, inputPeak, inputOverloads);
  chain_.processBlock(gainedInput_.data(), left, right, frames, cabLevelBlock_.data(), cabMixBlock_.data());
  float outputPeak = 0.0f;
  uint64_t outputOverloads = 0;
  for (size_t i = 0; i < frames; ++i) {
    const float output = smoothGain(currentOutputGain_, outputGain_)
                         * smoothGain(currentMasterVolume_, masterVolume_);
    const float master = currentMasterVolume_;
    const StereoSample mixed = equalPowerMix({sanitizedInput_[i] * master, sanitizedInput_[i] * master},
                                             {left[i] * output, right[i] * output}, smoothEffectsMix());
    const float framePeak = std::max(std::fabs(mixed.left), std::fabs(mixed.right));
    outputPeak = std::max(outputPeak, framePeak);
    outputOverloads += framePeak > 1.0f ? 1U : 0U;
    observeLimiter(mixed.left, mixed.right);
    left[i] = applySafety(mixed.left);
    right[i] = applySafety(mixed.right);
  }
  commitLevel(outputPeakBits_, outputOverloadFrames_, outputPeak, outputOverloads);
}

void PedalEngine::observeLimiter(float left, float right)
{
  if (!safetyLimiterEnabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const float limit = safetyLimit_.load(std::memory_order_relaxed);
  constexpr float kKneeFraction = 0.95f;
  if (limit > 0.0f && std::max(std::fabs(left), std::fabs(right)) > limit * kKneeFraction) {
    limiterFrames_.fetch_add(1, std::memory_order_relaxed);
  }
}

float PedalEngine::applySafety(float sample) const
{
  if (!std::isfinite(sample)) {
    return 0.0f;
  }
  if (!safetyLimiterEnabled_.load(std::memory_order_relaxed)) {
    return sample;
  }

  const float safetyLimit = safetyLimit_.load(std::memory_order_relaxed);
  if (safetyLimit <= 0.0f) {
    return sample;
  }

  // The final stage is a safeguard for the DAC, not an effect.  A hard clamp
  // here creates an unpleasant digital edge whenever a NAM/IR combination has
  // a transient above the preset ceiling.  Keep normal program material
  // bit-for-bit unchanged, then use a rational soft knee that is continuous
  // in both value and slope and asymptotically approaches the ceiling.  This
  // costs one divide only for an actual overload and needs no look-ahead,
  // allocation, or per-preset trim.
  constexpr float kKneeFraction = 0.95f;
  const float magnitude = std::fabs(sample);
  const float kneeStart = safetyLimit * kKneeFraction;
  if (magnitude <= kneeStart) {
    return sample;
  }

  const float kneeWidth = safetyLimit - kneeStart;
  const float normalizedExcess = (magnitude - kneeStart) / kneeWidth;
  const float limitedMagnitude = kneeStart + kneeWidth * normalizedExcess / (1.0f + normalizedExcess);
  return std::copysign(limitedMagnitude, sample);
}

} // namespace ardor
