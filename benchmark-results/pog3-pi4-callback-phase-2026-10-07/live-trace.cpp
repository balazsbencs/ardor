#include "daisyfx/pog3/PolyphonicPitchBank.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace ardor::pog3;
namespace {
constexpr double twoPi = 6.2831853071795864769;
float randomValue(std::uint32_t& state) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5;
  return static_cast<float>(state >> 8) / 16777216.0f;
}
void spectrum(std::vector<std::complex<float>>& values, unsigned tick, int shape,
              std::uint32_t& random) {
  const auto half = values.size() / 2;
  for (std::size_t k = 0; k <= half; ++k) {
    float amplitude = .0001f * randomValue(random);
    if (shape == 0) {
      // Sparse peaks own wide regions, with overlapping shifted destinations.
      const auto spacing = std::max<std::size_t>(half / 4, 8);
      const auto distance = std::min(k % spacing, spacing - k % spacing);
      amplitude += distance <= 2 ? .5f / (1 + distance) : .00001f;
    } else if (shape == 1) {
      amplitude += .05f * randomValue(random); // short regions and remainders
    } else {
      // DC/Nyquist ownership exercises reflection, endpoints and alias drops.
      amplitude += k <= 2 || k + 2 >= half ? .6f / (1 + std::min(k, half - k)) : .00001f;
    }
    const double angle = twoPi * k * tick / 8 + .23 * std::sin(.09 * tick + k);
    values[k] = {amplitude * static_cast<float>(std::cos(angle)),
                 k == 0 || k == half ? 0 : amplitude * static_cast<float>(std::sin(angle))};
    if (k && k < half) values[values.size() - k] = std::conj(values[k]);
  }
}
}
int main() {
  try {
    std::size_t samples = 0, batches = 0, pairs = 0, edges = 0, remainders = 0, integerSources = 0;
    // The deterministic shared fallback isolates renderer arithmetic from
    // independent FFTW planning. Production-size complete traces are separate.
    for (const std::size_t n : {64, 128, 256, 512}) for (const int shape : {0, 1, 2})
      for (const bool useLow : {false, true}) for (unsigned mode = 0; mode < 8; ++mode) {
        const auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(n, n / 8));
        const auto lowPlan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(512, 64));
        PitchFrame frame, lowFrame; frame.prepare(plan); lowFrame.prepare(lowPlan);
        PitchRenderer renderer; renderer.prepare(plan);
        std::vector<std::complex<float>> input(n), lowInput(512);
        PartialGains gains{}, lowGains{};
        std::uint32_t random = 0x53434154;
        float peak = 0;
        for (unsigned tick = 0; tick < 96; ++tick) {
          if (tick == 48) { frame.reset(); lowFrame.reset(); renderer.reset(); }
          spectrum(input, tick, shape, random); spectrum(lowInput, tick, shape, random);
          frame.update(input); lowFrame.update(lowInput);
          for (std::size_t track = 0; track < gains.size(); ++track) {
            gains[track] = (tick + track) % 11 == 0 ? 0 : .2f + .8f * randomValue(random);
            lowGains[track] = (tick + track) % 13 == 0 ? 0 : randomValue(random);
          }
          constexpr std::array<float, 7> fixed{-24, -12, 0, 7, 12, 24, -5.3f};
          const float semitones = mode < fixed.size() ? fixed[mode] : 24 * std::sin(.19f * tick);
          const float ratio = std::exp2(semitones / 12);
          // Fixture coverage follows the production region/bin bounds. It
          // does not instrument or alter either renderer under comparison.
          for (const auto& region : frame.regions()) {
            if (ratio != 1 && ratio * region.frequencyBins >= n / 2) continue;
            const float shift = (ratio - 1) * region.frequencyBins;
            const int ceil = static_cast<int>(std::ceil(shift));
            if (ceil - shift == 0) { integerSources += region.last - region.first + 1; continue; }
            for (int k = region.first; k <= region.last; ++k) {
              if (k + 3 <= region.last && k + ceil > PitchPlan::kRadius
                  && k + 3 + ceil + PitchPlan::kRadius - 1 < static_cast<int>(n / 2)) {
                ++batches; k += 3;
              } else if (k + 1 <= region.last && k + ceil > PitchPlan::kRadius
                         && k + 1 + ceil + PitchPlan::kRadius - 1 < static_cast<int>(n / 2)) {
                ++pairs; ++k;
              } else if (k + ceil > PitchPlan::kRadius
                         && k + ceil + PitchPlan::kRadius - 1 < static_cast<int>(n / 2)) ++remainders;
              else ++edges;
            }
          }
          if (!renderer.render(frame, semitones, useLow ? &lowFrame : nullptr, 0,
                               &gains, &lowGains, true)) throw std::runtime_error("live trace frame rejected");
          for (std::size_t k = 0; k < n / 8; ++k) {
            const float output = renderer.pop();
            if (!std::isfinite(output)) throw std::runtime_error("live trace output not finite");
            peak = std::max(peak, std::fabs(output));
            if (std::fwrite(&output, sizeof(output), 1, stdout) != 1) throw std::runtime_error("trace write failed");
            ++samples;
          }
        }
        if (peak == 0) throw std::runtime_error("trace never becomes audible");
        std::fprintf(stderr, "N=%zu shape=%d low=%d mode=%u peak=%.9g\n", n, shape, useLow, mode, peak);
      }
    if (!batches || !pairs || !edges || !remainders || !integerSources) throw std::runtime_error("missing trace coverage");
    std::fprintf(stderr, "samples=%zu batches=%zu pair_batches=%zu edge_sources=%zu remainder_sources=%zu integer_sources=%zu\n",
                 samples, batches, pairs, edges, remainders, integerSources);
    return 0;
  } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
