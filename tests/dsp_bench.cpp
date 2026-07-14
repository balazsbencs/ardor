// Per-component DSP microbenchmark. The realtime telemetry lumps NAM, the IR
// convolver, and gain staging into one number; this separates them so the Pi
// feasibility spike can see where the budget goes.
//
// Usage: pedal-dsp-bench [model.nam]
// Falls back to <source>/models/test.nam when no argument is given.

#include "dsp/IrConvolver.h"
#include "dsp/NamProcessor.h"
#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "dynamics/CompressorProcessor.h"
#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqProcessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr size_t kBlockSize = 64;
constexpr size_t kIrSamples = 8192;
constexpr double kSampleRate = 48000.0;
constexpr size_t kWarmupBlocks = 100;
constexpr size_t kTimedBlocks = 10000;
constexpr double kBudgetUs = kBlockSize / kSampleRate * 1e6; // ~1333 us

// Deterministic noise; keeps runs comparable across hosts.
float nextNoise(uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return (static_cast<float>(state >> 8) / static_cast<float>(1u << 24)) - 0.5f;
}

struct BenchResult {
  double minUs = 0.0;
  double avgUs = 0.0;
  double maxUs = 0.0;
};

template <typename ProcessBlock>
BenchResult bench(ProcessBlock&& processBlock)
{
  uint32_t noise = 0x1234567u;
  std::vector<float> in(kBlockSize, 0.0f);
  std::vector<float> out(kBlockSize, 0.0f);

  for (size_t b = 0; b < kWarmupBlocks; ++b) {
    for (auto& s : in) s = nextNoise(noise);
    processBlock(in.data(), out.data(), kBlockSize);
  }

  BenchResult result;
  result.minUs = 1e12;
  double totalUs = 0.0;
  for (size_t b = 0; b < kTimedBlocks; ++b) {
    for (auto& s : in) s = nextNoise(noise);
    const auto start = std::chrono::steady_clock::now();
    processBlock(in.data(), out.data(), kBlockSize);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double us = std::chrono::duration<double, std::micro>(elapsed).count();
    totalUs += us;
    result.minUs = std::min(result.minUs, us);
    result.maxUs = std::max(result.maxUs, us);
  }
  result.avgUs = totalUs / static_cast<double>(kTimedBlocks);
  return result;
}

void report(const char* name, const BenchResult& r)
{
  std::printf("%-14s min=%8.2fus avg=%8.2fus max=%8.2fus  budget%%=%5.1f\n",
              name, r.minUs, r.avgUs, r.maxUs, r.avgUs / kBudgetUs * 100.0);
}

} // namespace

int main(int argc, char** argv)
{
  std::printf("dsp-bench: block=%zu ir=%zu rate=%.0f budget=%.0fus blocks=%zu\n",
              kBlockSize, kIrSamples, kSampleRate, kBudgetUs, kTimedBlocks);

  {
    ardor::IrConvolver convolver;
    std::vector<float> impulse(kIrSamples, 0.0f);
    uint32_t noise = 0xdeadbeefu;
    for (auto& tap : impulse) tap = nextNoise(noise) * 0.1f;
    impulse[0] = 1.0f;
    convolver.loadImpulse(std::move(impulse));
    convolver.prepareBlockSize(kBlockSize);
    report("IrConvolver", bench([&](const float* in, float* out, size_t frames) {
      convolver.processBlock(in, out, frames);
    }));
  }

  std::filesystem::path modelPath = argc > 1
    ? std::filesystem::path{argv[1]}
    : std::filesystem::path{ARDOR_SOURCE_DIR} / "models/test.nam";
  if (std::filesystem::exists(modelPath)) {
    ardor::NamProcessor nam;
    if (!nam.load(modelPath, kSampleRate, static_cast<int>(kBlockSize))) {
      std::fprintf(stderr, "failed to load %s\n", modelPath.string().c_str());
      return 1;
    }
    const auto breakpoints = nam.slimmableSizeBreakpoints();
    if (breakpoints.empty()) {
      report("NamProcessor", bench([&](const float* in, float* out, size_t frames) {
        nam.processBlock(in, out, frames);
      }));
    } else {
      // SlimmableContainer: bench each tier. Tier i is selected by slim_val in
      // [breakpoints[i-1], breakpoints[i]). Use 0.0 for the first tier.
      const size_t tiers = breakpoints.size() + 1;
      for (size_t i = 0; i < tiers; ++i) {
        const double slim_val = (i == 0) ? 0.0 : breakpoints[i - 1];
        nam.setSlimmableSize(slim_val);
        char label[32];
        std::snprintf(label, sizeof(label), "NAM[tier-%zu]", i);
        report(label, bench([&](const float* in, float* out, size_t frames) {
          nam.processBlock(in, out, frames);
        }));
      }
    }
  } else {
    std::printf("NamProcessor  skipped (no model at %s)\n", modelPath.string().c_str());
  }

  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    ardor::DaisyFxProcessor effect;
    std::string error;
    if (!effect.configure(descriptor.blockType, ardor::defaultDaisyFxParams(descriptor),
                          static_cast<float>(kSampleRate), error)) {
      throw std::runtime_error(error);
    }
    const std::string label = descriptor.blockType + "/" + descriptor.mode;
    report(label.c_str(), bench([&](const float* in, float* out, size_t frames) {
      for (size_t i = 0; i < frames; ++i) {
        const auto sample = effect.process({in[i], in[i]});
        out[i] = 0.5f * (sample.left + sample.right);
      }
    }));
  }

  ardor::CompressorProcessor compressor;
  std::string compressorError;
  if (!compressor.configure({{"threshold_db", -24.0f}, {"ratio", 4.0f}, {"attack_ms", 10.0f},
                             {"release_ms", 150.0f}, {"knee_db", 6.0f}, {"makeup_db", 0.0f},
                             {"input_gain_db", 0.0f}, {"mix", 1.0f}, {"sidechain_hpf_hz", 80.0f},
                             {"detector", "peak"}, {"auto_makeup", false}},
                            static_cast<float>(kSampleRate), compressorError)) {
    throw std::runtime_error(compressorError);
  }
  report("dynamics/compressor", bench([&](const float* in, float* out, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
      const auto sample = compressor.process({in[i], in[i]});
      out[i] = 0.5f * (sample.left + sample.right);
    }
  }));

  ardor::ParametricEqProcessor equalizer;
  auto eqParams = ardor::defaultParametricEqParams();
  constexpr std::array<float, ardor::kParametricEqBandCount> eqGains = {6.0f, -6.0f, 9.0f, -9.0f, 12.0f};
  for (std::size_t i = 0; i < eqParams.bands.size(); ++i) {
    eqParams.bands[i].enabled = true;
    eqParams.bands[i].gainDb = eqGains[i];
  }
  std::string eqError;
  if (!equalizer.configure(eqParams, static_cast<float>(kSampleRate), eqError)) {
    throw std::runtime_error(eqError);
  }
  report("eq/parametric5", bench([&](const float* in, float* out, size_t frames) {
    std::array<float, kBlockSize> right{};
    std::copy(in, in + frames, right.begin());
    equalizer.processBlock(in, right.data(), out, right.data(), frames);
  }));

  return 0;
}
