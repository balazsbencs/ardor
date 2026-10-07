#include "daisyfx/pog3/PolyphonicPitchBank.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace ardor::pog3;
int main() {
  try {
    std::size_t samples = 0;
    // Small plans deliberately use the deterministic shared FFT fallback:
    // independently measured FFTW plans must not obscure arithmetic equality.
    for (const std::size_t n : {64, 128, 256}) for (const int bands : {1, 2, 3})
      for (const float semitones : {-24.0f, 0.0f, 7.0f, 24.0f}) {
        const auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(n, n / 8));
        PitchFrame frame; frame.prepare(plan);
        PitchRenderer renderer; renderer.prepare(plan, true);
        std::vector<std::complex<float>> silence(n);
        FrozenBand main, low, empty; main.count = low.count = 4;
        for (std::size_t j = 0; j < 4; ++j) {
          main.partials[j] = {.center=-static_cast<std::int64_t>(n), .id=j+1,
            .frequency=100.0f, .magnitude=.025f, .phase=static_cast<float>(j)*.37f};
          low.partials[j] = main.partials[j];
          low.partials[j].frequency = 350;
        }
        float peak = 0;
        for (std::size_t tick = 0; tick < 2048; ++tick) {
          // Enter and leave silent crossover bands without changing identities;
          // also keep histories warm through zero magnitude and zero held gain.
          const float ramp = static_cast<float>(tick % 512) / 511;
          main.partials[0].frequency = 100 + 250 * ramp;
          low.partials[0].frequency = 350 - 250 * ramp;
          main.partials[1].frequency = low.partials[1].frequency = 250;
          main.partials[1].magnitude = low.partials[1].magnitude = tick % 256 < 128 ? 0 : .025f;
          main.partials[2].frequency = low.partials[2].frequency = 1500;
          main.partials[3].frequency = low.partials[3].frequency = 7000;
          frame.update(silence);
          const float gain = tick % 384 < 128 ? 0 : .7f;
          if (!renderer.render(frame, semitones, nullptr, 0, nullptr, nullptr, true,
                               bands & 1 ? &main : &empty, bands & 2 ? &low : nullptr, 1, gain))
            throw std::runtime_error("held trace frame rejected");
          for (std::size_t k = 0; k < n / 8; ++k) {
            const float output = renderer.pop();
            if (!std::isfinite(output)) throw std::runtime_error("held trace output not finite");
            peak = std::max(peak, std::fabs(output));
            if (std::fwrite(&output, sizeof(output), 1, stdout) != 1)
              throw std::runtime_error("held trace write failed");
            ++samples;
          }
        }
        if (peak == 0) throw std::runtime_error("held trace never becomes audible");
        std::fprintf(stderr, "N=%zu bands=%d semitones=%.0f audible_peak=%.9g\n", n, bands, semitones, peak);
      }
    std::fprintf(stderr, "samples=%zu\n", samples);
    return 0;
  } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
