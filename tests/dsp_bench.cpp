// Per-component DSP microbenchmark. The realtime telemetry lumps NAM, the IR
// convolver, and gain staging into one number; this separates them so the Pi
// feasibility spike can see where the budget goes.
//
// Usage: pedal-dsp-bench [model.nam]
// Falls back to <source>/models/test.nam when no argument is given.

#include "dsp/IrConvolver.h"
#include "dsp/DenormalGuard.h"
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
  double p50Us = 0.0;
  double p99Us = 0.0;
  double p999Us = 0.0;
  double maxUs = 0.0;
};

struct SignalLevel {
  double rmsDb = 0.0;
  double peakDb = 0.0;
};

double percentile(std::vector<double> samples, double fraction)
{
  if (samples.empty()) {
    return 0.0;
  }
  std::sort(samples.begin(), samples.end());
  const auto index = static_cast<size_t>(std::ceil(fraction * static_cast<double>(samples.size() - 1)));
  return samples[index];
}

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
  std::vector<double> samples;
  samples.reserve(kTimedBlocks);
  double totalUs = 0.0;
  for (size_t b = 0; b < kTimedBlocks; ++b) {
    for (auto& s : in) s = nextNoise(noise);
    const auto start = std::chrono::steady_clock::now();
    processBlock(in.data(), out.data(), kBlockSize);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double us = std::chrono::duration<double, std::micro>(elapsed).count();
    samples.push_back(us);
    totalUs += us;
    result.minUs = std::min(result.minUs, us);
    result.maxUs = std::max(result.maxUs, us);
  }
  result.avgUs = totalUs / static_cast<double>(kTimedBlocks);
  result.p50Us = percentile(samples, 0.50);
  result.p99Us = percentile(samples, 0.99);
  result.p999Us = percentile(samples, 0.999);
  return result;
}

void report(const char* name, const BenchResult& r)
{
  std::printf("%-20s min=%7.2fus p50=%7.2fus p99=%7.2fus p999=%7.2fus max=%7.2fus avg=%7.2fus budget%%=%5.1f\n",
              name, r.minUs, r.p50Us, r.p99Us, r.p999Us, r.maxUs, r.avgUs,
              r.avgUs / kBudgetUs * 100.0);
}

SignalLevel measureDaisyWetLevel(const ardor::DaisyFxDescriptor& descriptor, bool periodicBursts)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  params["mix"] = 1.0f;
  // The hosted modulation range is 0..2, with 0.5 as unity.
  if (descriptor.kind == ardor::DaisyFxKind::Mod) {
    params["level"] = 0.5f;
  }
  ardor::DaisyFxProcessor effect;
  std::string error;
  if (!effect.configure(descriptor.blockType, params, static_cast<float>(kSampleRate), error)) {
    throw std::runtime_error(error);
  }

  constexpr size_t kSettleFrames = 48000;
  constexpr size_t kMeasureFrames = 48000;
  constexpr float kAmplitude = 0.25f;
  double sumSquares = 0.0;
  float peak = 0.0f;
  for (size_t i = 0; i < kSettleFrames + kMeasureFrames; ++i) {
    const bool noteOn = !periodicBursts || ((i % 24000U) < 2400U); // 50 ms every 500 ms
    const float input = noteOn
        ? kAmplitude * std::sin(6.28318530718f * 440.0f * static_cast<float>(i) /
                                  static_cast<float>(kSampleRate))
        : 0.0f;
    const auto output = effect.process({input, input});
    if (i >= kSettleFrames) {
      const float mono = 0.5f * (output.left + output.right);
      sumSquares += static_cast<double>(mono) * mono;
      peak = std::max(peak, std::fabs(mono));
    }
  }
  constexpr double kFloor = 1e-12;
  const double rms = std::sqrt(sumSquares / static_cast<double>(kMeasureFrames));
  return {20.0 * std::log10(std::max(rms, kFloor)),
          20.0 * std::log10(std::max(static_cast<double>(peak), kFloor))};
}

void reportDaisyWetLevel(const ardor::DaisyFxDescriptor& descriptor)
{
  const auto sustained = measureDaisyWetLevel(descriptor, false);
  const auto burst = measureDaisyWetLevel(descriptor, true);
  const std::string label = descriptor.blockType + "/" + descriptor.mode;
  std::printf("%-20s sustain=%6.2f/%6.2fdBFS burst=%6.2f/%6.2fdBFS\n", label.c_str(),
              sustained.rmsDb, sustained.peakDb, burst.rmsDb, burst.peakDb);
}

nlohmann::json stressDaisyFxParams(const ardor::DaisyFxDescriptor& descriptor)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  // Endpoint values maximize the active delay/reverb network and modulation
  // work without relying on host-specific presets. This is a CPU stress case,
  // not a recommended sound.
  for (const auto& param : descriptor.params) {
    params[param.key] = 1.0f;
  }
  return params;
}

ardor::DaisyFxProcessor makeDaisyEffect(const char* blockType, const char* mode, bool stress)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor(blockType, mode);
  if (!descriptor) {
    throw std::runtime_error(std::string{"missing Daisy descriptor: "} + blockType + "/" + mode);
  }
  ardor::DaisyFxProcessor effect;
  std::string error;
  const auto params = stress ? stressDaisyFxParams(*descriptor) : ardor::defaultDaisyFxParams(*descriptor);
  if (!effect.configure(descriptor->blockType, params, static_cast<float>(kSampleRate), error)) {
    throw std::runtime_error(error);
  }
  return effect;
}

} // namespace

int main(int argc, char** argv)
{
  const ardor::ScopedDenormalGuard denormalGuard;
#if defined(__aarch64__)
  constexpr const char* architecture = "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
  constexpr const char* architecture = "x86_64";
#elif defined(__arm__)
  constexpr const char* architecture = "arm";
#else
  constexpr const char* architecture = "unknown";
#endif
#if defined(NDEBUG)
  constexpr const char* assertions = "disabled";
#else
  constexpr const char* assertions = "enabled";
#endif
  std::printf("dsp-bench: build=%s assertions=%s compiler=%s arch=%s flags=%s\n",
              ARDOR_BUILD_TYPE, assertions, ARDOR_COMPILER, architecture, ARDOR_CXX_FLAGS);
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

    ardor::DaisyFxProcessor stressedEffect;
    if (!stressedEffect.configure(descriptor.blockType, stressDaisyFxParams(descriptor),
                                  static_cast<float>(kSampleRate), error)) {
      throw std::runtime_error(error);
    }
    const std::string stressLabel = label + " (stress)";
    report(stressLabel.c_str(), bench([&](const float* in, float* out, size_t frames) {
      for (size_t i = 0; i < frames; ++i) {
        const auto sample = stressedEffect.process({in[i], in[i]});
        out[i] = 0.5f * (sample.left + sample.right);
      }
    }));
  }

  std::printf("dsp-bench: Daisy full-wet 440 Hz level calibration (RMS/peak; after 1 s settle)\n");
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    reportDaisyWetLevel(descriptor);
  }

  // Representative hosted-only chains complement the full pedal measurements
  // above. They are useful for comparing release builds on the Pi without a
  // dependency on a particular NAM model or cabinet IR.
  {
    auto chorus = makeDaisyEffect("mod", "chorus", false);
    auto tape = makeDaisyEffect("delay", "tape", false);
    auto shimmer = makeDaisyEffect("reverb", "shimmer", false);
    report("Daisy ambient chain", bench([&](const float* in, float* out, size_t frames) {
      for (size_t i = 0; i < frames; ++i) {
        auto sample = chorus.process({in[i], in[i]});
        sample = tape.process(sample);
        sample = shimmer.process(sample);
        out[i] = 0.5f * (sample.left + sample.right);
      }
    }));
  }
  {
    auto octave = makeDaisyEffect("mod", "poly_octave", true);
    auto lofi = makeDaisyEffect("delay", "lofi", true);
    auto bloom = makeDaisyEffect("reverb", "bloom", true);
    report("Daisy heavy chain", bench([&](const float* in, float* out, size_t frames) {
      for (size_t i = 0; i < frames; ++i) {
        auto sample = octave.process({in[i], in[i]});
        sample = lofi.process(sample);
        sample = bloom.process(sample);
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
