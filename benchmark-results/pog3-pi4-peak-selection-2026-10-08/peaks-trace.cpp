#include "daisyfx/pog3/PolyphonicPitchBank.h"
#include <fftw3.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

template<class T> void field(const T& value) {
  std::cout.write(reinterpret_cast<const char*>(&value), sizeof(value));
}
int main(int argc, char** argv) {
  try {
    if (argc != 2 || !fftwf_import_wisdom_from_filename(argv[1]))
      throw std::runtime_error("requires FFTW seed wisdom");
    using namespace ardor::pog3;
    std::mt19937 random(0x4652414d);
    std::size_t frames = 0, regions = 0, overloaded = 0;
    for (const std::size_t n : {64, 1024, 2048, 4096, 32768}) {
      auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(n, n / 8));
      for (const std::size_t requested : {255, 256, 257, 258, 511, 513}) {
        PitchFrame cutoff;
        cutoff.prepare(plan);
        const auto peaks = std::min(requested, n / 4 + 1);
        for (unsigned shape = 0; shape < 5; ++shape) {
          std::vector<std::complex<float>> spectrum(n);
          for (std::size_t i = 0; i < peaks; ++i) {
            const float level = shape == 0 ? 1.0f
              : shape == 1 ? 1.0f + i / 32.0f
              : shape == 2 ? 1.0f + (peaks - i) / 32.0f
              : shape == 3 ? 1.0f + (i * 17 % 5) / 4.0f
              : 1.0f + (random() % 256) / 256.0f;
            spectrum[2 * i] = {level, 0};
          }
          for (unsigned history = 0; history < 3; ++history) {
            cutoff.update(spectrum);
            const std::uint64_t count = cutoff.regions().size(), capacity = cutoff.capacityEvents();
            const std::uint8_t valid = !cutoff.spectrum().empty();
            field(cutoff.centerSamples()); field(count); field(capacity); field(valid);
            for (const auto& region : cutoff.regions()) {
              field(region.bin); field(region.first); field(region.last); field(region.track);
              field(region.generation); field(region.frequencyBins); field(region.magnitude);
            }
            ++frames; regions += count; overloaded += capacity != 0;
          }
        }
      }
      for (const float ceiling : {400.0f, 24000.0f}) {
        PitchFrame frame;
        frame.prepare(plan, ceiling);
        std::vector<std::complex<float>> spectrum(n);
        for (unsigned step = 0; step < (n == 32768 ? 32U : 128U); ++step) {
          for (std::size_t k = 0; k < n; ++k) {
            const auto uniform = [&]() { return static_cast<float>(static_cast<int>(random() % 20001) - 10000) / 10000; };
            spectrum[k] = {uniform(), uniform()};
          }
          // Dense migrating peaks, steady equal magnitudes, sparse and silent
          // histories. Exercise changing predictions and repeated saturation.
          if (step % 16 == 0) std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
          if (step % 16 == 1 || step % 16 == 2) {
            std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
            for (std::size_t k = 0; k <= n / 2; k += 2)
              spectrum[k] = {(k % 4 ? -1.0f : 1.0f), (step == 2 ? .5f : 0.0f)};
          }
          if (step % 16 == 3) {
            std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
            spectrum[1] = {1e-7f, 0}; spectrum[3] = {1.0001e-7f, 0};
            spectrum[7] = {std::numeric_limits<float>::denorm_min(), 0};
            spectrum[n / 2] = {-1.0f, -0.0f};
          }
          if (step % 16 == 4) spectrum[0] = {std::numeric_limits<float>::quiet_NaN(), 0};
          if (step % 16 == 5) spectrum[0] = {std::numeric_limits<float>::infinity(), 0};
          if (step % 31 == 9) frame.reset();
          if (step % 37 == 11) frame.prepare(plan, ceiling);
          if (step % 23 == 7) frame.update(std::span(spectrum).first(n - 1));
          frame.update(spectrum);
          const std::uint64_t count = frame.regions().size(), capacity = frame.capacityEvents();
          const std::uint8_t valid = !frame.spectrum().empty();
          field(frame.centerSamples()); field(count); field(capacity); field(valid);
          if (count > kMaxPitchPartials) throw std::runtime_error("region bound");
          for (const auto& r : frame.regions()) {
            field(r.bin); field(r.first); field(r.last); field(r.track);
            field(r.generation); field(r.frequencyBins); field(r.magnitude);
          }
          ++frames; regions += count; overloaded += capacity != 0;
        }
      }
    }
    if (!std::cout || !overloaded) throw std::runtime_error("trace/overload coverage failed");
    std::cerr << "frames=" << frames << " regions=" << regions << " capacity-active-frames=" << overloaded << '\n';
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
