#include "dsp/PedalEngine.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

bool near(float left, float right)
{
  return std::fabs(left - right) < 0.0001f;
}

void require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  try {
    ardor::PedalEngine engine;
    engine.loadIr({1.0f});
    engine.setInputGain(1.0f);
    engine.setOutputGain(1.0f);
    engine.setMasterVolume(0.5f);

    auto [left, right] = engine.process(0.25f);
    require(near(left, 0.125f), "wet left sample");
    require(near(right, 0.125f), "wet right sample");

    engine.setEffectsBypassed(true);
    auto [dryLeft, dryRight] = engine.process(0.25f);
    require(near(dryLeft, 0.125f), "dry left sample");
    require(near(dryRight, 0.125f), "dry right sample");

    engine.setSafetyLimit(0.1f);
    auto [limitedLeft, limitedRight] = engine.process(1.0f);
    require(near(limitedLeft, 0.1f), "limited left sample");
    require(near(limitedRight, 0.1f), "limited right sample");

    std::vector<float> input{0.25f, -0.25f, 1.0f, -1.0f};
    std::vector<float> leftBlock(input.size(), 0.0f);
    std::vector<float> rightBlock(input.size(), 0.0f);
    engine.setEffectsBypassed(false);
    std::vector<float> wetInput{0.15f};
    std::vector<float> wetLeft(wetInput.size(), 0.0f);
    std::vector<float> wetRight(wetInput.size(), 0.0f);
    engine.processBlock(wetInput.data(), wetLeft.data(), wetRight.data(), wetInput.size());
    require(near(wetLeft[0], 0.075f), "wet block left sample");
    require(near(wetRight[0], 0.075f), "wet block right sample");

    engine.setEffectsBypassed(true);
    engine.processBlock(input.data(), leftBlock.data(), rightBlock.data(), input.size());
    require(near(leftBlock[0], 0.1f), "bypassed block left sample");
    require(near(rightBlock[1], -0.1f), "bypassed block right sample");

    ardor::PedalEngine historyEngine;
    historyEngine.loadIr({1.0f, 0.5f});
    historyEngine.setInputGain(1.0f);
    historyEngine.setOutputGain(1.0f);
    historyEngine.setMasterVolume(1.0f);
    historyEngine.setSafetyLimiterEnabled(false);
    std::vector<float> historyInput{1.0f};
    std::vector<float> historyLeft(historyInput.size(), 0.0f);
    std::vector<float> historyRight(historyInput.size(), 0.0f);
    historyEngine.processBlock(historyInput.data(), historyLeft.data(), historyRight.data(), historyInput.size());
    require(near(historyLeft[0], 1.0f), "history block left sample");
    require(near(historyRight[0], 1.0f), "history block right sample");
    historyEngine.setEffectsBypassed(true);
    historyEngine.setEffectsBypassed(false);
    std::vector<float> resetInput{0.0f};
    std::vector<float> resetLeft(resetInput.size(), 0.0f);
    std::vector<float> resetRight(resetInput.size(), 0.0f);
    historyEngine.processBlock(resetInput.data(), resetLeft.data(), resetRight.data(), resetInput.size());
    require(near(resetLeft[0], 0.0f), "reset block left sample");
    require(near(resetRight[0], 0.0f), "reset block right sample");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "engine_contract_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
