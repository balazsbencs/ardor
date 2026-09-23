#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/DualRigProcessor.h"
#include "dsp/PedalEngine.h"
#include "dsp/RuntimeChain.h"
#include "equalizer/EqParameters.h"
#include "tape/TapeProcessor.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

bool near(float left, float right)
{
  return std::fabs(left - right) < 0.0001f;
}

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

ardor::DaisyFxProcessor makeTrem()
{
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("mod", {
    {"mode", "vintage_trem"},
    {"speed", 0.8f},
    {"depth", 1.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"p1", 0.0f},
    {"p2", 0.0f},
    {"level", 1.0f},
  }, 48000.0f, error), error);
  return processor;
}

ardor::DaisyFxProcessor makeSwellReverb()
{
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("reverb", {
    {"mode", "swell"},
    {"decay", 0.45f},
    {"pre_delay", 0.15f},
    {"mix", 0.25f},
    {"tone", 0.5f},
    {"mod", 0.0f},
    {"param1", 0.5f},
    {"param2", 0.5f},
  }, 48000.0f, error), error);
  return processor;
}

ardor::DaisyFxProcessor makeDigitalDelay()
{
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("delay", {
    {"mode", "digital"}, {"time", 0.05f}, {"repeats", 0.65f}, {"mix", 1.0f},
    {"filter", 0.5f}, {"grit", 0.0f}, {"mod_spd", 0.0f}, {"mod_dep", 0.0f},
  }, 48000.0f, error), error);
  return processor;
}

std::vector<float> render(ardor::RuntimeChain& chain)
{
  std::vector<float> out;
  out.reserve(128);
  for (int i = 0; i < 128; ++i) {
    out.push_back(chain.process({0.5f, 0.5f}).left);
  }
  return out;
}

} // namespace

int main()
{
  require(near(ardor::routeNamInput(ardor::NamInputMode::Sum, 0.75f, -0.25f), 0.25f),
          "NAM sum input averages left and right without a gain increase");
  require(near(ardor::routeNamInput(ardor::NamInputMode::Left, 0.75f, -0.25f), 0.75f),
          "NAM left input preserves the left channel");
  require(near(ardor::routeNamInput(ardor::NamInputMode::Right, 0.75f, -0.25f), -0.25f),
          "NAM right input preserves the right channel");

  ardor::RuntimeChain modThenCab;
  modThenCab.prepareBlockSize(64);
  modThenCab.addDaisy("mod-a", makeTrem());
  modThenCab.addCab({1.0f, 0.5f}, 1.0f, 1.0f);

  ardor::RuntimeChain cabThenMod;
  cabThenMod.prepareBlockSize(64);
  cabThenMod.addCab({1.0f, 0.5f}, 1.0f, 1.0f);
  cabThenMod.addDaisy("mod-b", makeTrem());

  const auto a = render(modThenCab);
  const auto b = render(cabThenMod);
  float diff = 0.0f;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff += std::fabs(a[i] - b[i]);
  }
  require(diff > 0.0001f, "serial block order should change output");
  require(modThenCab.tailFrames() == 1, "cabinet tail contributes to the serial chain estimate");

  ardor::RuntimeChain clipChain;
  clipChain.addCab({2.0f}, 1.0f, 1.0f, "hot-cab");
  clipChain.process({0.75f, 0.75f});
  auto clipStages = clipChain.takeClipDiagnostics();
  require(clipStages.size() == 1, "clip diagnostics include each chain block");
  require(clipStages[0].kind == ardor::SignalStageKind::Cab && clipStages[0].id == "hot-cab",
          "clip diagnostics retain block kind and stable ID");
  require(near(clipStages[0].peak, 1.5f) && clipStages[0].overloadFrames == 1,
          "clip diagnostics report full-scale overload after IR");
  clipStages = clipChain.takeClipDiagnostics();
  require(near(clipStages[0].peak, 0.0f) && clipStages[0].overloadFrames == 0,
          "taking clip diagnostics starts a fresh interval");

  ardor::NoiseGateProcessor diagnosticGate;
  std::string diagnosticGateError;
  require(diagnosticGate.configure({{"mode", "noise_gate"}}, 48000.0f, diagnosticGateError),
          diagnosticGateError);
  ardor::RuntimeChain gateDiagnosticChain;
  gateDiagnosticChain.addNoiseGate("diagnostic-gate", std::move(diagnosticGate));
  gateDiagnosticChain.process({0.5f, 0.5f});
  const auto gateStages = gateDiagnosticChain.takeClipDiagnostics();
  require(gateStages.size() == 1
            && gateStages[0].kind == ardor::SignalStageKind::NoiseGate
            && gateStages[0].id == "diagnostic-gate",
          "clip diagnostics identify the noise gate stage and stable ID");

  ardor::DaisyFxProcessor delay;
  std::string delayError;
  require(delay.configure("delay", {
    {"mode", "digital"}, {"time", 0.0f}, {"repeats", 0.0f}, {"mix", 1.0f},
    {"filter", 0.5f}, {"grit", 0.0f}, {"mod_spd", 0.0f}, {"mod_dep", 0.0f},
  }, 48000.0f, delayError), delayError);
  const auto delayTail = delay.tailFrames();
  ardor::RuntimeChain cabThenDelay;
  cabThenDelay.prepareBlockSize(64);
  cabThenDelay.addCab({1.0f, 0.5f, 0.25f}, 1.0f, 1.0f);
  cabThenDelay.addDaisy("delay-a", std::move(delay));
  require(cabThenDelay.tailFrames() == 2 + delayTail,
          "serial cabinet and delay tails must accumulate");

  auto leftLane = std::make_unique<ardor::RuntimeChain>();
  leftLane->prepareBlockSize(64);
  leftLane->addCab({1.0f}, 1.0f, 1.0f, "left-cab");
  auto rightLane = std::make_unique<ardor::RuntimeChain>();
  rightLane->prepareBlockSize(64);
  rightLane->addCab({0.5f}, 1.0f, 1.0f, "right-cab");
  ardor::DualRigLaneConfig leftRigLane{std::move(leftLane), 1.0f, false};
  ardor::DualRigLaneConfig rightRigLane{std::move(rightLane), 1.0f, true};
  ardor::RuntimeChain dualRigChain;
  dualRigChain.prepareBlockSize(64);
  std::string dualRigError;
  require(dualRigChain.addDualRig("dual-rig", std::move(leftRigLane), std::move(rightRigLane),
                                  ardor::NamInputMode::Sum, 48000.0, 64, false, -1,
                                  dualRigError),
          dualRigError);
  std::array<float, 64> rigInput{};
  std::array<float, 64> rigLeft{};
  std::array<float, 64> rigRight{};
  rigInput.fill(0.5f);
  dualRigChain.processBlock(rigInput.data(), rigLeft.data(), rigRight.data(), rigInput.size());
  require(near(rigLeft[0], 0.5f), "dual rig keeps the left lane's left output");
  require(near(rigRight[0], -0.25f), "dual rig keeps the right lane's right output and polarity");
  require(dualRigChain.tailFrames() == 0, "dual rig tail is the maximum of its lane tails");

  auto nestedEffectLane = std::make_unique<ardor::RuntimeChain>();
  nestedEffectLane->prepareBlockSize(64);
  nestedEffectLane->addDaisy("nested-swell", makeSwellReverb());
  auto emptyLane = std::make_unique<ardor::RuntimeChain>();
  emptyLane->prepareBlockSize(64);
  ardor::RuntimeChain nestedControlChain;
  nestedControlChain.prepareBlockSize(64);
  require(nestedControlChain.addDualRig(
            "nested-control-rig",
            {std::move(nestedEffectLane), 1.0f, false},
            {std::move(emptyLane), 1.0f, false},
            ardor::NamInputMode::Sum, 48000.0, 64, false, -1, dualRigError),
          dualRigError);
  require(nestedControlChain.setDaisyParameter("nested-swell", "pre_delay", 0.8f),
          "Dual Rig should forward live Daisy parameters into its lanes");
  require(nestedControlChain.setBlockEnabled("nested-swell", false),
          "Dual Rig should forward live bypass into its lanes");
  require(!nestedControlChain.setDaisyParameter("missing", "pre_delay", 0.8f),
          "Dual Rig should still reject a missing nested effect ID");

  ardor::RuntimeChain eqChain;
  eqChain.prepareBlockSize(64);
  std::string eqError;
  auto eqParams = ardor::defaultParametricEqParams();
  eqParams.bands[2].gainDb = 6.0f;
  require(eqChain.addParametricEq("eq-a", eqParams, 48000.0f, eqError), eqError);
  require(eqChain.setParametricEqBand("eq-a", 2, {true, 1000.0f, 1.0f, 12.0f}),
          "target existing EQ by stable ID");
  require(!eqChain.setParametricEqBand("missing", 2, {true, 1000.0f, 1.0f, 12.0f}),
          "missing EQ ID rejected");
  require(!eqChain.setParametricEqBand("eq-a", 5, {true, 1000.0f, 1.0f, 12.0f}),
          "invalid EQ band rejected");
  require(eqChain.setParametricEqPassFilter(
            "eq-a", ardor::EqPassFilterKind::HighPass, {true, 80.0f, 0.70710678f}),
          "target existing EQ high-pass by stable ID");
  require(!eqChain.setParametricEqPassFilter(
            "missing", ardor::EqPassFilterKind::LowPass, {true, 12000.0f, 0.70710678f}),
          "missing EQ pass-filter ID rejected");

  ardor::PedalEngine engine;
  std::string error;
  require(engine.addDaisyFx("trem", "mod", {
    {"mode", "vintage_trem"},
    {"speed", 0.8f},
    {"depth", 1.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"p1", 0.0f},
    {"p2", 0.0f},
    {"level", 1.0f},
  }, 48000.0f, error), error);
  require(engine.setDaisyParameter("trem", "depth", 0.0f), "target Daisy by stable ID");
  require(!engine.setDaisyParameter("missing", "depth", 0.0f), "missing Daisy ID rejected");
  bool engineChanged = false;
  for (int i = 0; i < 128; ++i) {
    const auto wet = engine.process(0.5f);
    engineChanged = engineChanged || !near(wet.first, 0.5f);
  }
  require(engineChanged, "trem should affect engine output");
  require(engine.setBlockEnabled("trem", false), "scene should bypass a block by stable ID");
  float previousScene = engine.process(0.5f).first;
  ardor::StereoSample sceneDry{};
  for (int i = 0; i < 480; ++i) {
    const auto output = engine.process(0.5f);
    require(std::fabs(output.first - previousScene) < 0.2f,
            "scene bypass should not introduce a hard discontinuity");
    previousScene = output.first;
    sceneDry = {output.first, output.second};
  }
  require(near(sceneDry.left, 0.5f) && near(sceneDry.right, 0.5f),
          "scene-bypassed block should converge to dry audio");

  ardor::RuntimeChain cutTiming;
  cutTiming.addCab({0.0f}, 1.0f, 1.0f, "silent-cab");
  require(near(cutTiming.process({1.0f, 1.0f}).left, 0.0f),
          "cut timing fixture should begin fully processed");
  require(cutTiming.setBlockEnabled("silent-cab", false),
          "cut timing fixture should accept bypass");
  float cutValue = 0.0f;
  for (int frame = 0; frame < 240; ++frame) cutValue = cutTiming.process({1.0f, 1.0f}).left;
  require(std::fabs(cutValue - 0.5f) < 0.01f,
          "scene Cut should reach its midpoint after 5 ms");
  for (int frame = 240; frame < 480; ++frame) cutValue = cutTiming.process({1.0f, 1.0f}).left;
  require(near(cutValue, 1.0f), "scene Cut should reach dry after 10 ms");

  ardor::RuntimeChain cutTail;
  ardor::RuntimeChain tailReference;
  cutTail.addDaisy("delay", makeDigitalDelay());
  tailReference.addDaisy("delay", makeDigitalDelay());
  (void)cutTail.process({1.0f, 1.0f});
  (void)tailReference.process({1.0f, 1.0f});
  require(cutTail.setBlockEnabled("delay", false), "tail fixture should accept Cut bypass");
  for (int frame = 0; frame < 3000; ++frame) {
    (void)cutTail.process({});
    (void)tailReference.process({});
  }
  require(cutTail.setBlockEnabled("delay", true), "tail fixture should re-enable");
  for (int frame = 0; frame < 480; ++frame) {
    (void)cutTail.process({});
    (void)tailReference.process({});
  }
  for (int frame = 0; frame < 128; ++frame) {
    const auto resumed = cutTail.process({});
    const auto reference = tailReference.process({});
    require(near(resumed.left, reference.left) && near(resumed.right, reference.right),
            "Cut bypass must drain hidden time-effect history instead of freezing it");
  }
  require(!engine.setBlockEnabled("missing", true), "missing scene block ID rejected");
  require(engine.setBlockEnabled("trem", true), "scene should re-enable a prepared block");

  ardor::PedalEngine compressorEngine;
  require(compressorEngine.addCompressor("compressor", {
    {"threshold_db", -24.0f}, {"ratio", 8.0f}, {"attack_ms", 1.0f},
    {"release_ms", 100.0f}, {"mix", 1.0f}, {"sidechain_hpf_hz", 20.0f},
  }, 48000.0f, error), error);
  require(compressorEngine.setCompressorParameter("compressor", "mix", 0.5f), "target compressor by stable ID");
  require(!compressorEngine.setCompressorParameter("missing", "mix", 0.5f), "missing compressor ID rejected");
  float compressorOutput = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    compressorOutput = std::fabs(compressorEngine.process(i % 2 == 0 ? 1.0f : -1.0f).first);
  }
  require(compressorOutput < 0.7f, "compressor should affect engine output");
  require(compressorEngine.compressorGainReductionDb("compressor") < -1.0f,
          "gain-reduction telemetry should be reachable by stable ID through PedalEngine");
  require(compressorEngine.compressorGainReductionDb("missing") == 0.0f,
          "missing compressor ID should report no reduction rather than stale data");

  compressorEngine.setEffectsBypassed(true);
  ardor::StereoSample compressorDry{};
  for (int i = 0; i < 2400; ++i) {
    const auto output = compressorEngine.process(0.5f);
    compressorDry = {output.first, output.second};
  }
  require(near(compressorDry.left, 0.5f), "compressor bypass should return dry audio");

  ardor::PedalEngine transientShaperEngine;
  require(transientShaperEngine.addTransientShaper("transient-shaper", {
    {"attack", 100.0f}, {"sustain", 0.0f}, {"mix", 1.0f}, {"output_db", 0.0f},
  }, 48000.0f, error), error);
  require(transientShaperEngine.setTransientShaperParameter("transient-shaper", "sustain", -50.0f),
          "target transient shaper by stable ID");
  require(!transientShaperEngine.setTransientShaperParameter("missing", "attack", 10.0f),
          "missing transient shaper ID rejected");
  require(!transientShaperEngine.setTransientShaperParameter("transient-shaper", "nonsense", 1.0f),
          "unknown transient shaper parameter rejected");
  float shapedOutput = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    shapedOutput = transientShaperEngine.process(i % 2 == 0 ? 0.5f : -0.5f).first;
  }
  require(std::fabs(std::fabs(shapedOutput) - 0.5f) < 0.01f,
          "a held tone carries no transient, so the shaper must leave it alone");

  ardor::PedalEngine noiseGateEngine;
  require(noiseGateEngine.addNoiseGate("noise-gate", {
    {"threshold_db", -20.0f}, {"reduction_db", 80.0f}, {"attack_ms", 1.0f},
    {"hold_ms", 0.0f}, {"release_ms", 50.0f}, {"hysteresis_db", 6.0f},
    {"sidechain_hpf_hz", 80.0f},
  }, 48000.0f, error), error);
  require(noiseGateEngine.setNoiseGateParameter("noise-gate", "reduction_db", 60.0f),
          "target noise gate by stable ID");
  require(!noiseGateEngine.setNoiseGateParameter("missing", "threshold_db", -40.0f),
          "missing noise gate ID rejected");
  float gatedOutput = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    gatedOutput = std::fabs(noiseGateEngine.process(i % 2 == 0 ? 0.01f : -0.01f).first);
  }
  require(gatedOutput < 0.00002f, "noise gate should attenuate below-threshold input");
  for (int i = 0; i < 4800; ++i) {
    gatedOutput = std::fabs(noiseGateEngine.process(i % 2 == 0 ? 0.5f : -0.5f).first);
  }
  require(gatedOutput > 0.49f, "noise gate should pass above-threshold input");

  // A tape block must load through the distortion family, process, and take a
  // live parameter change — the same contract the rat and cheese blocks meet.
  {
    ardor::RuntimeChain tapeChain;
    ardor::TapeProcessor tape;
    std::string tapeError;
    nlohmann::json tapeParams;
    tapeParams["mode"] = "tape";
    tapeParams["drive"] = 6.0f;
    require(tape.configure(tapeParams, 48000.0f, tapeError), "tape must configure: " + tapeError);
    tapeChain.addDistortion("tape-1", std::move(tape));

    require(tapeChain.setDistortionParameter("tape-1", "drive", 12.0f),
            "a live drive change must reach the tape block");
    require(!tapeChain.setDistortionParameter("tape-1", "speed", 30.0f),
            "speed is a load-time choice, not a live control");
    require(!tapeChain.setDistortionParameter("missing", "drive", 0.0f),
            "an unknown block id must be rejected");

    for (int i = 0; i < 4096; ++i) {
      const float t = static_cast<float>(i) / 48000.0f;
      const float in = 0.4f * std::sin(6.28318530718f * 220.0f * t);
      const auto out = tapeChain.process({in, in});
      require(std::isfinite(out.left) && std::isfinite(out.right),
              "the tape block must stay finite in the chain");
    }
    tapeChain.reset();
  }

  // Let-ring scene bypass keeps the IR reverb's already-generated wet signal
  // audible after the 10 ms bypass transition. Cut drains the same internal
  // history silently.
  {
    std::vector<float> tailIr(4096);
    for (std::size_t i = 0; i < tailIr.size(); ++i) {
      tailIr[i] = 0.2f * std::exp(-4.0f * static_cast<float>(i) / tailIr.size());
    }
    ardor::RuntimeChain letRing;
    ardor::RuntimeChain letRingWithDry;
    ardor::RuntimeChain cut;
    require(letRing.addIrReverb("ring", tailIr, {}, 48000.0f, error, true), error);
    require(letRingWithDry.addIrReverb("ring-dry", tailIr, {}, 48000.0f, error, true), error);
    require(cut.addIrReverb("cut", tailIr, {}, 48000.0f, error, false), error);
    require(letRing.setIrReverbParameter("ring", "mix", 1.0f), "set let-ring mix");
    require(letRingWithDry.setIrReverbParameter("ring-dry", "mix", 1.0f), "set dry-probe mix");
    require(cut.setIrReverbParameter("cut", "mix", 1.0f), "set cut mix");
    letRing.reset();
    letRingWithDry.reset();
    cut.reset();
    for (int i = 0; i < 256; ++i) {
      const float input = i == 0 ? 1.0f : 0.0f;
      (void)letRing.process({input, input});
      (void)letRingWithDry.process({input, input});
      (void)cut.process({input, input});
    }
    ardor::SceneRuntimeAddress bypassAddress;
    bypassAddress.kind = ardor::SceneRuntimeTargetKind::BlockEnabled;
    require(letRing.applySceneTarget(bypassAddress, 0.0f), "scene-disable let-ring reverb");
    require(letRingWithDry.applySceneTarget(bypassAddress, 0.0f), "scene-disable dry-probe reverb");
    require(cut.applySceneTarget(bypassAddress, 0.0f), "scene-disable cut reverb");
    float ringPeak = 0.0f;
    float cutPeak = 0.0f;
    float worstDryError = 0.0f;
    for (int i = 0; i < 1400; ++i) {
      const auto ringOut = letRing.process({});
      const float dryInput = i >= 500 ? 0.25f : 0.0f;
      const auto dryOut = letRingWithDry.process({dryInput, dryInput});
      const auto cutOut = cut.process({});
      if (i >= 500) {
        ringPeak = std::max(ringPeak, std::fabs(ringOut.left));
        cutPeak = std::max(cutPeak, std::fabs(cutOut.left));
        worstDryError = std::max(worstDryError,
          std::fabs((dryOut.left - ringOut.left) - dryInput));
      }
    }
    require(ringPeak > 1.0e-4f, "let ring should preserve wet output after bypass settles");
    require(cutPeak < 1.0e-6f, "cut should silence wet output after bypass settles");
    require(worstDryError < 1.0e-5f, "let ring should add exactly one live dry path");

    require(letRing.applySceneTarget(bypassAddress, 1.0f), "scene re-enable let-ring reverb");
    float previous = letRing.process({}).left;
    for (int i = 0; i < 512; ++i) {
      const float current = letRing.process({}).left;
      require(std::isfinite(current), "let-ring re-enable stays finite");
      require(std::fabs(current - previous) < 0.25f,
              "let-ring re-enable avoids a hard discontinuity");
      previous = current;
    }

    ardor::RuntimeChain blockRing;
    require(blockRing.addIrReverb("block-ring", tailIr, {}, 48000.0f, error, true), error);
    require(blockRing.setIrReverbParameter("block-ring", "mix", 1.0f), "set block let-ring mix");
    blockRing.prepareBlockSize(64);
    blockRing.reset();
    float input[64]{};
    float left[64]{};
    float right[64]{};
    input[0] = 1.0f;
    blockRing.processBlock(input, left, right, 64);
    input[0] = 0.0f;
    for (int i = 0; i < 3; ++i) blockRing.processBlock(input, left, right, 64);
    require(blockRing.applySceneTarget(bypassAddress, 0.0f), "block scene-disable let-ring reverb");
    float blockTailPeak = 0.0f;
    for (int block = 0; block < 20; ++block) {
      blockRing.processBlock(input, left, right, 64);
      if (block >= 8) {
        for (float sample : left) blockTailPeak = std::max(blockTailPeak, std::fabs(sample));
      }
    }
    require(blockTailPeak > 1.0e-4f, "block processing should preserve the let-ring tail");

    const nlohmann::json delayParams = {
      {"mode", "digital"}, {"time", 0.05f}, {"repeats", 0.65f},
      {"mix", 1.0f}, {"filter", 0.5f}, {"grit", 0.0f},
      {"mod_spd", 0.0f}, {"mod_dep", 0.0f},
    };
    ardor::DaisyFxProcessor delayProcessor;
    ardor::DaisyFxProcessor cutDelayProcessor;
    require(delayProcessor.configure("delay", delayParams, 48000.0f, error), error);
    require(cutDelayProcessor.configure("delay", delayParams, 48000.0f, error), error);
    ardor::RuntimeChain delayRing;
    ardor::RuntimeChain delayCut;
    delayRing.addDaisy("delay-ring", std::move(delayProcessor), true);
    delayCut.addDaisy("delay-cut", std::move(cutDelayProcessor), false);
    for (int i = 0; i < 128; ++i) {
      const float sample = i == 0 ? 1.0f : 0.0f;
      (void)delayRing.process({sample, sample});
      (void)delayCut.process({sample, sample});
    }
    require(delayRing.applySceneTarget(bypassAddress, 0.0f), "scene-disable let-ring delay");
    require(delayCut.applySceneTarget(bypassAddress, 0.0f), "scene-disable cut delay");
    float delayRingPeak = 0.0f;
    float delayCutPeak = 0.0f;
    for (int i = 0; i < 12000; ++i) {
      const auto ringOut = delayRing.process({});
      const auto cutOut = delayCut.process({});
      if (i >= 500) {
        delayRingPeak = std::max(delayRingPeak, std::fabs(ringOut.left));
        delayCutPeak = std::max(delayCutPeak, std::fabs(cutOut.left));
      }
    }
    require(delayRingPeak > 1.0e-4f, "hosted delay let ring should preserve repeats");
    require(delayCutPeak < 1.0e-6f, "hosted delay Cut should silence settled repeats");

    const nlohmann::json reverbParams = {
      {"mode", "room"}, {"decay", 0.45f}, {"pre_delay", 0.0f},
      {"mix", 1.0f}, {"tone", 0.5f}, {"mod", 0.2f},
      {"param1", 0.0f}, {"param2", 0.0f},
    };
    ardor::DaisyFxProcessor reverbProcessor;
    require(reverbProcessor.configure("reverb", reverbParams, 48000.0f, error), error);
    ardor::RuntimeChain reverbRing;
    reverbRing.addDaisy("reverb-ring", std::move(reverbProcessor), true);
    for (int i = 0; i < 512; ++i) {
      const float sample = i == 0 ? 1.0f : 0.0f;
      (void)reverbRing.process({sample, sample});
    }
    require(reverbRing.applySceneTarget(bypassAddress, 0.0f), "scene-disable let-ring reverb");
    float hostedReverbPeak = 0.0f;
    for (int i = 0; i < 12000; ++i) {
      const auto output = reverbRing.process({});
      if (i >= 500) hostedReverbPeak = std::max(hostedReverbPeak, std::fabs(output.left));
    }
    require(hostedReverbPeak > 1.0e-5f, "hosted reverb let ring should preserve decay");
  }

  // Live controls are written by the UI/control thread while DSP runs on the
  // callback thread. Exercise that ownership boundary directly so this test is
  // useful under ThreadSanitizer as well as checking finite output normally.
  {
    ardor::PedalEngine automationEngine;
    automationEngine.prepareBlockSize(64);
    automationEngine.setSafetyLimiterEnabled(false);
    require(automationEngine.addDistortion("live-tape", {{"mode", "tape"}}, 48000.0f, error), error);
    require(automationEngine.addIrReverb("live-ir", {1.0f}, {}, 48000.0f, error), error);
    require(automationEngine.addStereoWidener("live-width", 48000.0f, error), error);
    require(automationEngine.addTransientShaper("live-transient", {}, 48000.0f, error), error);

    std::atomic<bool> startAutomation{false};
    std::thread controls([&] {
      while (!startAutomation.load(std::memory_order_acquire)) std::this_thread::yield();
      for (int i = 0; i < 512; ++i) {
        const float unit = static_cast<float>(i & 63) / 63.0f;
        require(automationEngine.setDistortionParameter("live-tape", "drive", -12.0f + 36.0f * unit),
                "concurrent tape automation should be accepted");
        require(automationEngine.setIrReverbParameter("live-ir", "mix", unit),
                "concurrent IR automation should be accepted");
        require(automationEngine.setStereoWidenerParameter("live-width", "width", 2.0f * unit),
                "concurrent width automation should be accepted");
        require(automationEngine.setTransientShaperParameter("live-transient", "attack", 200.0f * unit - 100.0f),
                "concurrent transient automation should be accepted");
      }
    });

    float blockInput[64]{};
    float blockLeft[64]{};
    float blockRight[64]{};
    startAutomation.store(true, std::memory_order_release);
    for (int block = 0; block < 512; ++block) {
      for (int i = 0; i < 64; ++i) blockInput[i] = ((block * 64 + i) & 1) ? 0.1f : -0.1f;
      automationEngine.processBlock(blockInput, blockLeft, blockRight, 64);
      require(std::isfinite(blockLeft[63]) && std::isfinite(blockRight[63]),
              "concurrent live automation must keep DSP output finite");
    }
    controls.join();
  }

  engine.setEffectsBypassed(true);
  ardor::StereoSample dry{};
  for (int i = 0; i < 2400; ++i) {
    const auto output = engine.process(0.5f);
    dry = {output.first, output.second};
  }
  require(near(dry.left, 0.5f), "bypass should return dry left");
  require(near(dry.right, 0.5f), "bypass should return dry right");

  // A bypass request is a short unity-preserving transition. It must remain finite
  // and converge to the dry signal without a hard state reset.
  engine.setEffectsBypassed(false);
  float previous = engine.process(0.5f).first;
  engine.setEffectsBypassed(true);
  for (int i = 0; i < 512; ++i) {
    const float current = engine.process(0.5f).first;
    require(std::isfinite(current), "bypass transition output finite");
    require(std::fabs(current - previous) < 0.2f, "bypass transition avoids a hard discontinuity");
    previous = current;
  }
}
