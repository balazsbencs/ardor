#include "PedalEngine.h"

#include <algorithm>
#include <utility>

namespace ardor {

bool PedalEngine::loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize)
{
  return nam_.load(modelPath, sampleRate, maxBlockSize);
}

void PedalEngine::loadIr(std::vector<float> impulse)
{
  ir_.loadImpulse(std::move(impulse));
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

void PedalEngine::reset()
{
  nam_.reset();
  ir_.reset();
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
  const float afterNam = nam_.process(afterGain);
  const float wet = applySafety(ir_.processSample(afterNam) * outputGain_.load(std::memory_order_relaxed) * masterVolume);
  return {wet, wet};
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

  if (namBlock_.size() < frames) {
    namBlock_.resize(frames);
    irBlock_.resize(frames);
  }

  const float inputGain = inputGain_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < frames; ++i) {
    namBlock_[i] = nam_.process(input[i] * inputGain);
  }

  ir_.processBlock(namBlock_.data(), irBlock_.data(), frames);

  const float outputGain = outputGain_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < frames; ++i) {
    const float wet = applySafety(irBlock_[i] * outputGain * masterVolume);
    left[i] = wet;
    right[i] = wet;
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
