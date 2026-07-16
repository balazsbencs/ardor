#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/PedalEngine.h"
#include "dsp/RuntimeChain.h"
#include "equalizer/EqParameters.h"

#include <cmath>
#include <stdexcept>
#include <string>
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

  compressorEngine.setEffectsBypassed(true);
  ardor::StereoSample compressorDry{};
  for (int i = 0; i < 2400; ++i) {
    const auto output = compressorEngine.process(0.5f);
    compressorDry = {output.first, output.second};
  }
  require(near(compressorDry.left, 0.5f), "compressor bypass should return dry audio");

  engine.setEffectsBypassed(true);
  ardor::StereoSample dry{};
  for (int i = 0; i < 2400; ++i) {
    const auto output = engine.process(0.5f);
    dry = {output.first, output.second};
  }
  require(near(dry.left, 0.5f), "bypass should return dry left");
  require(near(dry.right, 0.5f), "bypass should return dry right");

  // A bypass request is a short equal-power transition. It must remain finite
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
