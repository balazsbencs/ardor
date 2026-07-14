#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/PedalEngine.h"
#include "dsp/RuntimeChain.h"

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
  modThenCab.addDaisy(makeTrem());
  modThenCab.addCab({1.0f, 0.5f}, 1.0f, 1.0f);

  ardor::RuntimeChain cabThenMod;
  cabThenMod.prepareBlockSize(64);
  cabThenMod.addCab({1.0f, 0.5f}, 1.0f, 1.0f);
  cabThenMod.addDaisy(makeTrem());

  const auto a = render(modThenCab);
  const auto b = render(cabThenMod);
  float diff = 0.0f;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff += std::fabs(a[i] - b[i]);
  }
  require(diff > 0.0001f, "serial block order should change output");

  ardor::PedalEngine engine;
  std::string error;
  require(engine.addDaisyFx("mod", {
    {"mode", "vintage_trem"},
    {"speed", 0.8f},
    {"depth", 1.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"p1", 0.0f},
    {"p2", 0.0f},
    {"level", 1.0f},
  }, 48000.0f, error), error);
  bool engineChanged = false;
  for (int i = 0; i < 128; ++i) {
    const auto wet = engine.process(0.5f);
    engineChanged = engineChanged || !near(wet.first, 0.5f);
  }
  require(engineChanged, "trem should affect engine output");

  ardor::PedalEngine compressorEngine;
  require(compressorEngine.addCompressor({
    {"threshold_db", -24.0f}, {"ratio", 8.0f}, {"attack_ms", 1.0f},
    {"release_ms", 100.0f}, {"mix", 1.0f}, {"sidechain_hpf_hz", 20.0f},
  }, 48000.0f, error), error);
  float compressorOutput = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    compressorOutput = std::fabs(compressorEngine.process(i % 2 == 0 ? 1.0f : -1.0f).first);
  }
  require(compressorOutput < 0.7f, "compressor should affect engine output");

  compressorEngine.setEffectsBypassed(true);
  const auto compressorDry = compressorEngine.process(0.5f);
  require(near(compressorDry.first, 0.5f), "compressor bypass should return dry audio");

  engine.setEffectsBypassed(true);
  const auto dry = engine.process(0.5f);
  require(near(dry.first, 0.5f), "bypass should return dry left");
  require(near(dry.second, 0.5f), "bypass should return dry right");

  engine.setEffectsBypassed(false);
  std::vector<float> resetWetA;
  for (int i = 0; i < 16; ++i) {
    resetWetA.push_back(engine.process(0.5f).first);
  }
  engine.setEffectsBypassed(true);
  engine.setEffectsBypassed(false);
  for (int i = 0; i < 16; ++i) {
    require(near(resetWetA[static_cast<std::size_t>(i)], engine.process(0.5f).first), "bypass should reset block state");
  }
}
