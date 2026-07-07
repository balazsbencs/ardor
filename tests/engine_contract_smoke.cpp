#include "dsp/PedalEngine.h"

#include <cassert>
#include <cmath>
#include <vector>

namespace {

bool near(float left, float right)
{
  return std::fabs(left - right) < 0.0001f;
}

} // namespace

int main()
{
  ardor::PedalEngine engine;
  engine.loadIr({1.0f});
  engine.setInputGain(1.0f);
  engine.setOutputGain(1.0f);
  engine.setMasterVolume(0.5f);

  auto [left, right] = engine.process(0.25f);
  assert(near(left, 0.125f));
  assert(near(right, 0.125f));

  engine.setEffectsBypassed(true);
  auto [dryLeft, dryRight] = engine.process(0.25f);
  assert(near(dryLeft, 0.125f));
  assert(near(dryRight, 0.125f));

  engine.setSafetyLimit(0.1f);
  auto [limitedLeft, limitedRight] = engine.process(1.0f);
  assert(near(limitedLeft, 0.1f));
  assert(near(limitedRight, 0.1f));

  std::vector<float> input{0.25f, -0.25f, 1.0f, -1.0f};
  std::vector<float> leftBlock(input.size(), 0.0f);
  std::vector<float> rightBlock(input.size(), 0.0f);
  engine.setEffectsBypassed(false);
  std::vector<float> wetInput{0.15f};
  std::vector<float> wetLeft(wetInput.size(), 0.0f);
  std::vector<float> wetRight(wetInput.size(), 0.0f);
  engine.processBlock(wetInput.data(), wetLeft.data(), wetRight.data(), wetInput.size());
  assert(near(wetLeft[0], 0.075f));
  assert(near(wetRight[0], 0.075f));

  engine.setEffectsBypassed(true);
  engine.processBlock(input.data(), leftBlock.data(), rightBlock.data(), input.size());
  assert(near(leftBlock[0], 0.1f));
  assert(near(rightBlock[1], -0.1f));

  ardor::PedalEngine historyEngine;
  historyEngine.loadIr({1.0f, 0.5f});
  historyEngine.setInputGain(1.0f);
  historyEngine.setOutputGain(1.0f);
  historyEngine.setMasterVolume(1.0f);
  std::vector<float> historyInput{1.0f};
  std::vector<float> historyLeft(historyInput.size(), 0.0f);
  std::vector<float> historyRight(historyInput.size(), 0.0f);
  historyEngine.processBlock(historyInput.data(), historyLeft.data(), historyRight.data(), historyInput.size());
  assert(near(historyLeft[0], 1.0f));
  assert(near(historyRight[0], 1.0f));
  historyEngine.setEffectsBypassed(true);
  historyEngine.setEffectsBypassed(false);
  std::vector<float> resetInput{0.0f};
  std::vector<float> resetLeft(resetInput.size(), 0.0f);
  std::vector<float> resetRight(resetInput.size(), 0.0f);
  historyEngine.processBlock(resetInput.data(), resetLeft.data(), resetRight.data(), resetInput.size());
  assert(near(resetLeft[0], 0.0f));
  assert(near(resetRight[0], 0.0f));
  return 0;
}
