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
  engine.processBlock(input.data(), leftBlock.data(), rightBlock.data(), input.size());
  assert(near(leftBlock[0], 0.1f));
  assert(near(rightBlock[1], -0.1f));
  return 0;
}
