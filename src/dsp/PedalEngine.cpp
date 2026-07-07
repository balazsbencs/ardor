#include "PedalEngine.h"

#include <utility>

namespace ardor {

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
  const float wet = ir_.processSample(input * inputGain_) * outputGain_;
  return {wet, wet};
}

} // namespace ardor
