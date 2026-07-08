// Per-component DSP microbenchmark. The realtime telemetry lumps NAM, the IR
// convolver, and gain staging into one number; this separates them so the Pi
// feasibility spike can see where the budget goes.
//
// Usage: pedal-dsp-bench [model.nam]
// Falls back to <source>/models/test.nam when no argument is given.

#include "dsp/IrConvolver.h"
#include "dsp/NamProcessor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
    report("NamProcessor", bench([&](const float* in, float* out, size_t frames) {
      nam.processBlock(in, out, frames);
    }));
  } else {
    std::printf("NamProcessor  skipped (no model at %s)\n", modelPath.string().c_str());
  }

  return 0;
}
