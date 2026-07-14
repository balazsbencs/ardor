#include "PedalEngine.h"

#include "daisyfx/DaisyFxProcessor.h"
#include "dynamics/CompressorProcessor.h"
#include "equalizer/EqParameters.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ardor {

void PedalEngine::setSampleRate(double sampleRate)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
    return;
  }
  sampleRate_ = static_cast<float>(sampleRate);
  constexpr float kGainSmoothingSeconds = 0.005f;
  gainSmoothingCoefficient_ = 1.0f - std::exp(-1.0f / (kGainSmoothingSeconds * sampleRate_));
}

bool PedalEngine::loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize)
{
  setSampleRate(sampleRate);
  prepareBlockSize(static_cast<size_t>(std::max(1, maxBlockSize)));
  return chain_.addNam(modelPath, sampleRate, maxBlockSize);
}

void PedalEngine::loadIr(std::vector<float> impulse)
{
  addCab(std::move(impulse), cabLevel_.load(std::memory_order_relaxed), cabMix_.load(std::memory_order_relaxed));
}

void PedalEngine::addCab(std::vector<float> impulse, float level, float mix)
{
  const float safeLevel = std::isfinite(level) ? std::max(0.0f, level) : 1.0f;
  const float safeMix = std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 1.0f;
  cabLevel_.store(safeLevel, std::memory_order_relaxed);
  cabMix_.store(safeMix, std::memory_order_relaxed);
  chain_.addCab(std::move(impulse), safeLevel, safeMix);
}

bool PedalEngine::addDaisyFx(const std::string& blockType, const nlohmann::json& params, float sampleRate, std::string& error)
{
  DaisyFxProcessor processor;
  if (!processor.configure(blockType, params, sampleRate, error)) {
    return false;
  }
  chain_.addDaisy(std::move(processor));
  return true;
}

bool PedalEngine::addCompressor(const nlohmann::json& params, float sampleRate, std::string& error)
{
  CompressorProcessor processor;
  if (!processor.configure(params, sampleRate, error)) {
    return false;
  }
  chain_.addCompressor(std::move(processor));
  return true;
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

void PedalEngine::replacePreparedProgram(PedalEngine&& prepared)
{
  chain_ = std::move(prepared.chain_);
  blockSize_ = prepared.blockSize_;
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
  beginAudioProcessing();
  const float masterVolume = smoothGain(currentMasterVolume_, masterVolume_);
  const float afterGain = input * smoothGain(currentInputGain_, inputGain_);
  const auto wet = chain_.process({afterGain, afterGain}, smoothGain(currentCabLevel_, cabLevel_),
                                  smoothGain(currentCabMix_, cabMix_));
  const float output = smoothGain(currentOutputGain_, outputGain_) * masterVolume;
  const StereoSample mixed = equalPowerMix({input * masterVolume, input * masterVolume},
                                           {wet.left * output, wet.right * output}, smoothEffectsMix());
  return {applySafety(mixed.left), applySafety(mixed.right)};
}

void PedalEngine::processBlock(const float* input, float* left, float* right, size_t frames)
{
  beginAudioProcessing();
  if (blockSize_ == 0) {
    prepareBlockSize(frames);
  }
  if (frames > blockSize_) {
    size_t offset = 0;
    while (offset < frames) {
      const size_t chunk = std::min(blockSize_, frames - offset);
      processBlock(input + offset, left + offset, right + offset, chunk);
      offset += chunk;
    }
    return;
  }

  for (size_t i = 0; i < frames; ++i) {
    gainedInput_[i] = input[i] * smoothGain(currentInputGain_, inputGain_);
    cabLevelBlock_[i] = smoothGain(currentCabLevel_, cabLevel_);
    cabMixBlock_[i] = smoothGain(currentCabMix_, cabMix_);
  }
  chain_.processBlock(gainedInput_.data(), left, right, frames, cabLevelBlock_.data(), cabMixBlock_.data());
  for (size_t i = 0; i < frames; ++i) {
    const float output = smoothGain(currentOutputGain_, outputGain_)
                         * smoothGain(currentMasterVolume_, masterVolume_);
    const float master = currentMasterVolume_;
    const StereoSample mixed = equalPowerMix({input[i] * master, input[i] * master},
                                             {left[i] * output, right[i] * output}, smoothEffectsMix());
    left[i] = applySafety(mixed.left);
    right[i] = applySafety(mixed.right);
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
  return std::clamp(sample, -safetyLimit, safetyLimit);
}

} // namespace ardor
