#include "PedalEngine.h"

#include "daisyfx/DaisyFxProcessor.h"

#include <algorithm>
#include <utility>

namespace ardor {

bool PedalEngine::loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize)
{
  prepareBlockSize(static_cast<size_t>(std::max(1, maxBlockSize)));
  return chain_.addNam(modelPath, sampleRate, maxBlockSize);
}

void PedalEngine::loadIr(std::vector<float> impulse)
{
  addCab(std::move(impulse), cabLevel_.load(std::memory_order_relaxed), cabMix_.load(std::memory_order_relaxed));
}

void PedalEngine::addCab(std::vector<float> impulse, float level, float mix)
{
  chain_.addCab(std::move(impulse), level, mix);
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

void PedalEngine::prepareBlockSize(size_t frames)
{
  if (frames == 0 || blockSize_ == frames) {
    return;
  }
  blockSize_ = frames;
  chain_.prepareBlockSize(frames);
}

void PedalEngine::clearEffects()
{
  chain_.clear();
}

void PedalEngine::setInputGain(float gain)
{
  inputGain_.store(gain, std::memory_order_relaxed);
}

void PedalEngine::setOutputGain(float gain)
{
  outputGain_.store(gain, std::memory_order_relaxed);
}

void PedalEngine::setMasterVolume(float gain)
{
  masterVolume_.store(std::max(0.0f, gain), std::memory_order_relaxed);
}

void PedalEngine::setEffectsBypassed(bool bypassed)
{
  const bool previous = effectsBypassed_.exchange(bypassed, std::memory_order_relaxed);
  if (previous != bypassed) {
    resetRequested_.store(true, std::memory_order_relaxed);
  }
}

void PedalEngine::setSafetyLimit(float limit)
{
  safetyLimit_.store(std::max(0.0f, limit), std::memory_order_relaxed);
}

void PedalEngine::setSafetyLimiterEnabled(bool enabled)
{
  safetyLimiterEnabled_.store(enabled, std::memory_order_relaxed);
}

void PedalEngine::setCabLevel(float gain)
{
  const float level = std::max(0.0f, gain);
  cabLevel_.store(level, std::memory_order_relaxed);
  chain_.setCabParams(level, cabMix_.load(std::memory_order_relaxed));
}

void PedalEngine::setCabMix(float mix)
{
  const float cabMix = std::clamp(mix, 0.0f, 1.0f);
  cabMix_.store(cabMix, std::memory_order_relaxed);
  chain_.setCabParams(cabLevel_.load(std::memory_order_relaxed), cabMix);
}

void PedalEngine::reset()
{
  chain_.reset();
}

void PedalEngine::applyPendingReset()
{
  if (resetRequested_.exchange(false, std::memory_order_relaxed)) {
    reset();
  }
}

std::pair<float, float> PedalEngine::process(float input)
{
  applyPendingReset();

  const float masterVolume = masterVolume_.load(std::memory_order_relaxed);
  if (effectsBypassed_.load(std::memory_order_relaxed)) {
    const float dry = applySafety(input * masterVolume);
    return {dry, dry};
  }

  const float afterGain = input * inputGain_.load(std::memory_order_relaxed);
  const auto wet = chain_.process({afterGain, afterGain});
  const float output = outputGain_.load(std::memory_order_relaxed) * masterVolume;
  return {applySafety(wet.left * output), applySafety(wet.right * output)};
}

void PedalEngine::processBlock(const float* input, float* left, float* right, size_t frames)
{
  applyPendingReset();

  const float masterVolume = masterVolume_.load(std::memory_order_relaxed);
  if (effectsBypassed_.load(std::memory_order_relaxed)) {
    for (size_t i = 0; i < frames; ++i) {
      const float dry = applySafety(input[i] * masterVolume);
      left[i] = dry;
      right[i] = dry;
    }
    return;
  }

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

  const float inputGain = inputGain_.load(std::memory_order_relaxed);
  const float outputGain = outputGain_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < frames; ++i) {
    const auto wet = chain_.process({input[i] * inputGain, input[i] * inputGain});
    left[i] = applySafety(wet.left * outputGain * masterVolume);
    right[i] = applySafety(wet.right * outputGain * masterVolume);
  }
}

float PedalEngine::applySafety(float sample) const
{
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
