#include "RateAdapter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using ardor::clap_audio::RateAdapter;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Engine {
  unsigned frames = 0;
  static void clean(void* context, const float* input, float* left, float* right, unsigned count, int) {
    static_cast<Engine*>(context)->frames += count;
    std::copy_n(input, count, left); std::copy_n(input, count, right);
  }
};
double tone(RateAdapter& adapter, double rate, double frequency) {
  adapter.reset(0); Engine engine;
  double energy = 0; unsigned samples = 0;
  for (unsigned i = 0; i < static_cast<unsigned>(rate / 2); ++i) {
    const float input = .1f * std::sin(2 * 3.141592653589793 * frequency * i / rate);
    RateAdapter::Output out;
    require(adapter.tick(input, input, 0, Engine::clean, &engine, out), "Tone conversion under/overflowed");
    if (i > rate / 4) { energy += out.left * out.left; ++samples; }
  }
  return std::sqrt(energy / samples);
}
}
int main() {
  try {
    for (double rate : {1000., 1234.5678, 8000., 12345., 12345.678, 22050., 32000., 44100., 45678.901, 48000., 50000., 88200., 96000., 123456.78, 176400., 192000., 384000., 768000.}) {
      RateAdapter adapter; adapter.prepare(rate, 0); adapter.reset(0); Engine engine;
      std::vector<float> impulse(adapter.latency() + 4096);
      const auto start = std::chrono::steady_clock::now();
      for (unsigned i = 0; i < impulse.size(); ++i) {
        RateAdapter::Output out;
        require(adapter.tick(i == 0 ? .1f : 0, i == 0 ? .1f : 0, 0, Engine::clean, &engine, out), "Impulse conversion under/overflowed");
        impulse[i] = out.left;
        require(out.dry == (i == adapter.latency() ? .1f : 0), "Dry bypass not delayed by reported host latency");
        require(out.left == out.right, "Stereo converters lost phase alignment");
      }
      const auto peak = std::max_element(impulse.begin(), impulse.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
      const auto peakFrame = static_cast<unsigned>(peak - impulse.begin());
      require(peakFrame == adapter.latency(), "Impulse peak differs from reported latency");
      const double rms = tone(adapter, rate, std::min(1000., rate / 8));
      require(std::abs(rms - .1 / std::sqrt(2.)) < .0001, "Passband gain changed");
      if (rate >= 44100) require(std::abs(tone(adapter, rate, 20000) - .1 / std::sqrt(2.)) < .001, "20 kHz passband attenuated");
      if (rate >= 88200) require(tone(adapter, rate, 25000) < .000003, "Downsampling failed to reject ultrasonic aliasing");
      // A continuous two-second stream verifies that fractional ratios never
      // accumulate frame-count drift. Reset must restart every converter phase.
      adapter.reset(0); engine.frames = 0;
      const unsigned frames = static_cast<unsigned>(2 * rate);
      for (unsigned i = 0; i < frames; ++i) {
        RateAdapter::Output out;
        require(adapter.tick(0, 0, 0, Engine::clean, &engine, out), "Long stream conversion drifted");
        require(out.left == 0 && out.right == 0 && out.dry == 0, "Reset retained FIR/queue history");
      }
      require(std::abs(double(engine.frames) - double(frames) * 48000 / rate) <= 65, "Engine did not run at 48 kHz");
      const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::cout << rate << " Hz: " << adapter.latency() << " host frames latency, passband RMS " << rms << ", test CPU " << seconds << " s\n";
    }
    require(!RateAdapter::supports(0) && !RateAdapter::supports(999) && !RateAdapter::supports(768001), "Invalid rate accepted");
    std::mt19937 random(73);
    std::uniform_real_distribution<double> rates(1000, 768000);
    for (unsigned pass = 0; pass < 100; ++pass) {
      const double rate = pass == 0 ? 47999.999 : pass == 1 ? 48000.001 : rates(random);
      RateAdapter adapter; adapter.prepare(rate, 0); adapter.reset(0); Engine engine;
      for (unsigned frame = 0; frame < 20000; ++frame) {
        RateAdapter::Output out;
        require(adapter.tick(frame == 0 ? .1f : 0, 0, 0, Engine::clean, &engine, out), "Random fractional ratio under/overflowed");
        require(std::isfinite(out.left) && out.left == out.right, "Random fractional ratio corrupted stereo output");
      }
    }
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
