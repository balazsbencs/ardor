#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<ardor::StereoSample> renderBlock(ardor::DaisyFxProcessor& processor, std::size_t frames)
{
  std::vector<ardor::StereoSample> out;
  out.reserve(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    out.push_back(processor.process({0.5f, 0.5f}));
  }
  return out;
}

} // namespace

int main()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    ardor::DaisyFxProcessor catalogProcessor;
    std::string catalogError;
    require(catalogProcessor.configure(descriptor.blockType, ardor::defaultDaisyFxParams(descriptor),
                                       48000.0f, catalogError),
            catalogError);
    const auto sample = catalogProcessor.process({0.5f, 0.5f});
    require(std::isfinite(sample.left) && std::isfinite(sample.right), "catalog output finite");
  }

  ardor::DaisyFxProcessor processor;
  std::string error;
  const nlohmann::json params{
    {"mode", "vintage_trem"},
    {"speed", 0.7f},
    {"depth", 1.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"p1", 0.0f},
    {"p2", 0.0f},
    {"level", 1.0f},
  };

  require(processor.configure("mod", params, 48000.0f, error), error);
  require(processor.tailFrames() == 0, "modulation has no offline tail estimate");
  require(!processor.configure("mod", params, 44100.0f, error), "non-48 kHz Daisy configuration should fail");
  require(error.find("48000") != std::string::npos, "non-48 kHz Daisy error should explain the constraint");
  require(processor.configure("mod", params, 48000.0f, error), error);
  const auto first = renderBlock(processor, 256);
  bool changed = false;
  for (const auto& sample : first) {
    require(std::isfinite(sample.left), "left output finite");
    require(std::isfinite(sample.right), "right output finite");
    require(std::fabs(sample.left) <= 1.0f, "left output bounded");
    require(std::fabs(sample.right) <= 1.0f, "right output bounded");
    changed = changed || std::fabs(sample.left - 0.5f) > 0.0001f;
  }
  require(changed, "vintage trem should change steady input");

  // Modulation state (notably LFO phase) is also per processor. Advancing one
  // instance must not alter a newly configured sibling's first output.
  ardor::DaisyFxProcessor modA;
  ardor::DaisyFxProcessor modB;
  ardor::DaisyFxProcessor modReference;
  require(modA.configure("mod", params, 48000.0f, error), error);
  require(modB.configure("mod", params, 48000.0f, error), error);
  require(modReference.configure("mod", params, 48000.0f, error), error);
  for (int i = 0; i < 137; ++i) {
    (void)modA.process({0.5f, 0.5f});
  }
  const auto isolatedMod = modB.process({0.5f, 0.5f});
  const auto referenceMod = modReference.process({0.5f, 0.5f});
  require(std::fabs(isolatedMod.left - referenceMod.left) < 0.000001f,
          "same-mode modulation instances must have independent phase state");
  require(std::fabs(isolatedMod.right - referenceMod.right) < 0.000001f,
          "same-mode modulation instances must have independent phase state");

  require(processor.setParameterTarget("depth", 0.0f), "publish live modulation target");
  require(!processor.setParameterTarget("mode", 0.0f), "mode is not a live target");
  const auto liveTargetOutput = renderBlock(processor, 48);
  require(std::isfinite(liveTargetOutput.back().left), "live target output finite");

  processor.reset();
  const auto afterResetA = renderBlock(processor, 64);
  processor.reset();
  const auto afterResetB = renderBlock(processor, 64);
  require(afterResetA.size() == afterResetB.size(), "reset block sizes");
  for (std::size_t i = 0; i < afterResetA.size(); ++i) {
    require(afterResetA[i].left == afterResetB[i].left, "reset left deterministic");
    require(afterResetA[i].right == afterResetB[i].right, "reset right deterministic");
  }

  nlohmann::json bad = params;
  bad["mode"] = "bogus";
  require(!processor.configure("mod", bad, 48000.0f, error), "unknown mode should fail");
  require(error.find("unsupported") != std::string::npos, "unknown mode error");

  nlohmann::json nonFinite = params;
  nonFinite["depth"] = std::numeric_limits<float>::infinity();
  require(!processor.configure("mod", nonFinite, 48000.0f, error), "non-finite Daisy parameter should fail");
  require(error.find("finite") != std::string::npos, "non-finite Daisy parameter error");

  const nlohmann::json delayParams{
    {"mode", "digital"},
    {"time", 0.02f},
    {"repeats", 0.35f},
    {"mix", 1.0f},
    {"filter", 0.5f},
    {"grit", 0.0f},
    {"mod_spd", 0.0f},
    {"mod_dep", 0.0f},
  };
  require(processor.configure("delay", delayParams, 48000.0f, error), error);
  const auto delayTail = processor.tailFrames();
  require(delayTail > 0 && delayTail < 60U * 48000U, "delay tail estimate is bounded");
  require(processor.setParameterTarget("time", 1.0f), "publish maximum delay time");
  require(processor.setParameterTarget("repeats", 1.0f), "publish maximum delay repeats");
  require(processor.tailFrames() == 60U * 48000U, "extreme delay tail estimate is capped");
  bool delayChanged = false;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    require(std::isfinite(sample.left), "delay left finite");
    require(std::isfinite(sample.right), "delay right finite");
    delayChanged = delayChanged || std::fabs(sample.left) > 0.0001f || std::fabs(sample.right) > 0.0001f;
  }
  require(delayChanged, "digital delay should produce wet output");

  // Two effects of the same kind must not share delay storage. The silent
  // processor is stepped after the excited one so shared backing storage
  // would leak the first processor's repeats into its output.
  ardor::DaisyFxProcessor delayA;
  ardor::DaisyFxProcessor delayB;
  require(delayA.configure("delay", delayParams, 48000.0f, error), error);
  require(delayB.configure("delay", delayParams, 48000.0f, error), error);
  float delayATail = 0.0f;
  float delayBPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto a = delayA.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    const auto b = delayB.process({0.0f, 0.0f});
    delayATail = std::max(delayATail, std::max(std::fabs(a.left), std::fabs(a.right)));
    delayBPeak = std::max(delayBPeak, std::max(std::fabs(b.left), std::fabs(b.right)));
  }
  require(delayATail > 0.0001f, "excited delay should produce a tail");
  require(delayBPeak < 0.000001f, "same-mode delay instances must be isolated");

  const nlohmann::json reverbParams{
    {"mode", "room"},
    {"decay", 0.45f},
    {"pre_delay", 0.0f},
    {"mix", 1.0f},
    {"tone", 0.5f},
    {"mod", 0.0f},
    {"param1", 0.5f},
    {"param2", 0.5f},
  };
  require(processor.configure("reverb", reverbParams, 48000.0f, error), error);
  require(processor.tailFrames() > 0, "reverb has an offline tail estimate");
  bool reverbChanged = false;
  for (int i = 0; i < 2048; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    require(std::isfinite(sample.left), "reverb left finite");
    require(std::isfinite(sample.right), "reverb right finite");
    reverbChanged = reverbChanged || std::fabs(sample.left) > 0.0001f || std::fabs(sample.right) > 0.0001f;
  }
  require(reverbChanged, "room reverb should produce wet output");

  ardor::DaisyFxProcessor roomA;
  ardor::DaisyFxProcessor roomB;
  require(roomA.configure("reverb", reverbParams, 48000.0f, error), error);
  require(roomB.configure("reverb", reverbParams, 48000.0f, error), error);
  float roomATail = 0.0f;
  float roomBPeak = 0.0f;
  for (int i = 0; i < 4096; ++i) {
    const auto a = roomA.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    const auto b = roomB.process({0.0f, 0.0f});
    roomATail = std::max(roomATail, std::max(std::fabs(a.left), std::fabs(a.right)));
    roomBPeak = std::max(roomBPeak, std::max(std::fabs(b.left), std::fabs(b.right)));
  }
  require(roomATail > 0.0001f, "excited room reverb should produce a tail");
  require(roomBPeak < 0.000001f, "same-mode reverb instances must be isolated");
}
