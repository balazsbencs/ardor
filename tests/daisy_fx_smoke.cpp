#include "daisyfx/DaisyFxProcessor.h"

#include <cmath>
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
  bool delayChanged = false;
  for (int i = 0; i < 4096; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    require(std::isfinite(sample.left), "delay left finite");
    require(std::isfinite(sample.right), "delay right finite");
    delayChanged = delayChanged || std::fabs(sample.left) > 0.0001f || std::fabs(sample.right) > 0.0001f;
  }
  require(delayChanged, "digital delay should produce wet output");

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
  bool reverbChanged = false;
  for (int i = 0; i < 2048; ++i) {
    const auto sample = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    require(std::isfinite(sample.left), "reverb left finite");
    require(std::isfinite(sample.right), "reverb right finite");
    reverbChanged = reverbChanged || std::fabs(sample.left) > 0.0001f || std::fabs(sample.right) > 0.0001f;
  }
  require(reverbChanged, "room reverb should produce wet output");
}
