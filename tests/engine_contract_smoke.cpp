#include "dsp/PedalEngine.h"
#include "dsp/DenormalGuard.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
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
    const bool flushToZeroWasEnabled = ardor::flushToZeroEnabled();
    {
      const ardor::ScopedDenormalGuard denormalGuard;
      require(ardor::flushToZeroEnabled(), "denormal guard enables flush-to-zero");
    }
    require(ardor::flushToZeroEnabled() == flushToZeroWasEnabled,
            "denormal guard restores the caller floating-point mode");

    ardor::PedalEngine engine;
    engine.loadIr({1.0f});
    engine.setInputGain(1.0f);
    engine.setOutputGain(1.0f);
    engine.setMasterVolume(0.5f);

    auto [left, right] = engine.process(0.25f);
    require(near(left, 0.125f), "wet left sample");
    require(near(right, 0.125f), "wet right sample");

    engine.setEffectsBypassed(true);
    float dryLeft = 0.0f;
    float dryRight = 0.0f;
    for (int i = 0; i < 2400; ++i) {
      std::tie(dryLeft, dryRight) = engine.process(0.25f);
    }
    require(near(dryLeft, 0.125f), "dry left sample");
    require(near(dryRight, 0.125f), "dry right sample");

    engine.setSafetyLimit(0.1f);
    auto [limitedLeft, limitedRight] = engine.process(1.0f);
    require(limitedLeft > 0.095f && limitedLeft < 0.1f, "safety limiter should smoothly contain left overload");
    require(limitedRight > 0.095f && limitedRight < 0.1f, "safety limiter should smoothly contain right overload");

    std::vector<float> input{0.25f, -0.25f, 1.0f, -1.0f};
    std::vector<float> leftBlock(input.size(), 0.0f);
    std::vector<float> rightBlock(input.size(), 0.0f);
    engine.setEffectsBypassed(false);
    for (int i = 0; i < 2400; ++i) {
      engine.process(0.0f);
    }
    std::vector<float> wetInput{0.15f};
    std::vector<float> wetLeft(wetInput.size(), 0.0f);
    std::vector<float> wetRight(wetInput.size(), 0.0f);
    engine.processBlock(wetInput.data(), wetLeft.data(), wetRight.data(), wetInput.size());
    require(near(wetLeft[0], 0.075f), "wet block left sample");
    require(near(wetRight[0], 0.075f), "wet block right sample");

    engine.setEffectsBypassed(true);
    engine.prepareBlockSize(input.size());
    engine.processBlock(input.data(), leftBlock.data(), rightBlock.data(), input.size());
    for (int i = 0; i < 512; ++i) {
      engine.process(0.0f);
    }
    engine.processBlock(input.data(), leftBlock.data(), rightBlock.data(), input.size());
    require(leftBlock[0] > 0.095f && leftBlock[0] < 0.1f,
            "bypassed block should be smoothly contained");
    require(rightBlock[1] < -0.095f && rightBlock[1] > -0.1f,
            "bypassed block should preserve polarity through the limiter");

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
    require(near(resetLeft[0], 0.5f), "bypass should preserve and continue cab tail left");
    require(near(resetRight[0], 0.5f), "bypass should preserve and continue cab tail right");

    ardor::PedalEngine cabMixEngine;
    cabMixEngine.loadIr({1.0f});
    cabMixEngine.setSafetyLimiterEnabled(false);
    cabMixEngine.setCabLevel(0.5f);
    cabMixEngine.setCabMix(0.5f);
    const auto mixed = cabMixEngine.process(1.0f);
    require(near(mixed.first, 0.75f), "cab mix should blend dry and wet");

    ardor::PedalEngine safetyEngine;
    safetyEngine.setSafetyLimiterEnabled(false);
    safetyEngine.setInputGain(std::nanf(""));
    safetyEngine.setOutputGain(std::nanf(""));
    const auto finiteGain = safetyEngine.process(0.25f);
    require(near(finiteGain.first, 0.25f), "non-finite gain must fall back safely");
    const auto finiteOutput = safetyEngine.process(std::nanf(""));
    require(near(finiteOutput.first, 0.0f), "non-finite audio must be silenced");
    require(safetyEngine.nonFiniteInputSamples() == 1, "non-finite sample counter");
    const float invalidBlock[] = {std::nanf(""), 0.25f};
    float invalidLeft[2]{};
    float invalidRight[2]{};
    safetyEngine.processBlock(invalidBlock, invalidLeft, invalidRight, 2);
    require(std::isfinite(invalidLeft[0]) && std::isfinite(invalidRight[0]),
            "non-finite block input must be contained");
    require(safetyEngine.nonFiniteInputSamples() == 2, "non-finite block counter");

    ardor::PedalEngine diagnosticEngine;
    diagnosticEngine.prepareBlockSize(2);
    diagnosticEngine.addCab({2.0f}, 1.0f, 1.0f, "diagnostic-cab");
    const float diagnosticInput[] = {0.75f, 0.25f};
    float diagnosticLeft[2]{};
    float diagnosticRight[2]{};
    diagnosticEngine.processBlock(diagnosticInput, diagnosticLeft, diagnosticRight, 2);
    const auto diagnostics = diagnosticEngine.takeClipDiagnostics();
    require(diagnostics.stages.size() == 3, "engine diagnostics include input, block, and output");
    require(diagnostics.stages[0].kind == ardor::SignalStageKind::Input
              && diagnostics.stages[0].overloadFrames == 0,
            "input boundary remains below full scale");
    require(diagnostics.stages[1].kind == ardor::SignalStageKind::Cab
              && diagnostics.stages[1].id == "diagnostic-cab"
              && diagnostics.stages[1].overloadFrames == 1,
            "IR is identified as the first overloaded block");
    require(diagnostics.stages[2].kind == ardor::SignalStageKind::Output
              && diagnostics.stages[2].overloadFrames == 1,
            "pre-limiter output overload remains observable");
    require(diagnostics.limiterFrames == 1, "safety limiter activity is counted once per frame");
    require(ardor::formatClipDiagnostics(diagnostics).find("first=ir:diagnostic-cab") != std::string::npos,
            "formatted diagnostics name the first overloaded stage");

    ardor::PedalEngine smoothingEngine;
    smoothingEngine.setSafetyLimiterEnabled(false);
    const auto beforeChange = smoothingEngine.process(0.5f);
    require(near(beforeChange.first, 0.5f), "gain smoothing initial value");
    smoothingEngine.setOutputGain(0.0f);
    const auto firstSmoothed = smoothingEngine.process(0.5f);
    require(firstSmoothed.first > 0.0f && firstSmoothed.first < 0.5f,
            "live gain change should ramp instead of stepping");
    float settled = firstSmoothed.first;
    for (int i = 0; i < 1200; ++i) {
      settled = smoothingEngine.process(0.5f).first;
    }
    require(std::fabs(settled) < 0.01f, "gain smoothing should converge to its requested value");

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "engine_contract_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
