#include "PedalEngine.h"

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
  inputGain_ = gain;
}

void PedalEngine::setOutputGain(float gain)
{
  outputGain_ = gain;
}

void PedalEngine::reset()
{
  ir_.reset();
}

std::pair<float, float> PedalEngine::process(float input)
{
  const float afterGain = input * inputGain_;
  const float afterNam = nam_.process(afterGain);
  const float wet = ir_.processSample(afterNam) * outputGain_;
  return {wet, wet};
}

void PedalEngine::processBlock(const float* input, float* left, float* right, size_t frames)
{
  if (namBlock_.size() < frames) {
    namBlock_.resize(frames);
    irBlock_.resize(frames);
  }

  for (size_t i = 0; i < frames; ++i) {
    namBlock_[i] = nam_.process(input[i] * inputGain_);
  }

  ir_.processBlock(namBlock_.data(), irBlock_.data(), frames);

  for (size_t i = 0; i < frames; ++i) {
    const float wet = irBlock_[i] * outputGain_;
    left[i] = wet;
    right[i] = wet;
  }
}

} // namespace ardor
