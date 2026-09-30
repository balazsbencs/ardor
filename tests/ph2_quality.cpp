#include "mod_effect_test_support.h"
#include "daisyfx/hosted/modes/ph2_mode.h"
#include <chrono>
#include <complex>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
using namespace mod_test;
using C = std::complex<double>;

void circuitResponse()
{
  // Independent small-signal circuit analysis: use impedances in the s plane,
  // then the bilinear s substitution. No production allpass/table helper.
  const double fs = 96000.0;
  for (bool mode2 : {false, true}) for (double beta : {0.0, 10000.0 / 14700.0}) {
    for (double hz : {150.0, 900.0, 4000.0}) {
      const C z = std::polar(1.0, -kTwoPi * hz / fs);
      const C s = 2.0 * fs * (1.0 - z) / (1.0 + z);
      const double swept_g = std::tan(kTwoPi * 1000.0 / (2.0 * fs));
      const double fixed_g = std::tan(1.0 / (2.0 * fs * 5600.0 * 1e-8));
      const auto ap = [s,fs](double g) { return (s - 2.0 * fs * g) / (s + 2.0 * fs * g); };
      const auto hp = [s](double rc) { return s * rc / (1.0 + s * rc); };
      const auto loop = [&](C input, bool second, int count, double resonance) {
        C phase = std::pow(ap(swept_g), count - 2) * std::pow(ap(fixed_g), 2);
        phase *= hp(68000.0e-6) * hp(10000.0e-6);
        if (count == 10) phase *= hp(68000.0e-6);
        const C feedback = 1.0 / (1.0 + s * 4700.0e-9);
        const double cap = second ? 2.7e-9 : 4.7e-9;
        const C returned = (4700.0 / 11500.0) / (1.0 + s * (6800.0 * 4700.0 / 11500.0) * cap);
        const C wet = -input * phase * feedback / (1.0 - resonance * returned * (1.0 + feedback) * phase);
        const double mix_wet = 4700.0 / (second ? 10000.0 : 18000.0);
        const double mix_dry = (4700.0 / (second ? 16700.0 : 26700.0)) * (1.0 + mix_wet);
        return mix_dry * input - mix_wet * wet;
      };
      C expected = loop(1.0, false, mode2 ? 6 : 10, beta);
      if (mode2) expected = loop(expected, true, 6, 1.0);
      pedal::ph2::Routing routing;
      C measured{};
      constexpr int frames = 192000;
      for (int i = 0; i < frames; ++i) {
        constexpr float amplitude = 1e-4f;
        const float x = amplitude * std::cos(kTwoPi * hz * i / fs);
        const float y = routing.process(x, swept_g, fixed_g, beta, mode2);
        if (i >= frames / 2) measured += (2.0 * y / amplitude) * std::polar(1.0, -kTwoPi * hz * i / fs) / (frames / 2.0);
      }
      require(std::abs(measured - expected) < 0.003,
              "circuit response Mode " + std::to_string(mode2 ? 2 : 1) + " at " + fmt(hz) +
              " error " + fmt(std::abs(measured - expected)));
      require(routing.first.max_residual < 2e-6f && routing.second.max_residual < 2e-6f,
              "small-signal feedback solver converges");
    }
  }
}

void companderDynamics()
{
  const auto compression = [](float input) {
    pedal::ph2::Compander c;
    float y = 0.0f;
    for (int i = 0; i < 96000; ++i) y = c.compress(input);
    return y;
  };
  const float low = compression(0.01f), high = compression(0.1f);
  require(std::abs(high / low - std::sqrt(10.0f)) < 0.01f, "NE571 compression is 2:1");
  require(std::abs(low * low / 0.01f - (28.0f / 94.0f)) < 0.002f, "compressor resistor scaling");
  const auto expansion = [](float input) {
    pedal::ph2::Compander c;
    float y = 0.0f;
    for (int i = 0; i < 96000; ++i) y = c.expand(input);
    return y;
  };
  require(std::abs(expansion(0.2f) / expansion(0.02f) - 100.0f) < 0.05f, "NE571 expansion is 1:2");
  pedal::ph2::Compander c;
  for (int i = 0; i < 96000; ++i) c.expand(0.1f);
  const float settled = c.expansion_envelope;
  for (int i = 0; i < 4512; ++i) c.expand(0.0f);
  require(std::abs(c.expansion_envelope / settled - std::exp(-1.0f)) < 0.002f,
          "NE571 4.7u rectifier has 47ms decay");
  require(c.expand(0.0f) == 0.0f && c.compress(0.0f) == 0.0f, "compander silence is exact");
}

void sweepAndReset()
{
  // Verify the resampling latency itself, independently of the dry-delay
  // declaration. Even-phase decimation must land its symmetric peak at 15.
  pedal::HalfbandInterpolator2x up;
  pedal::HalfbandDecimator2x down;
  float ignored; down.Push(0.0f, ignored);
  float peak = 0.0f, sum = 0.0f; int peak_index = -1;
  for (int i = 0; i < 64; ++i) {
    float y = 0.0f;
    for (float x : up.Process(i == 0 ? 1.0f : 0.0f))
      if (down.Push(x, ignored)) y = ignored;
    sum += y;
    if (std::abs(y) > peak) { peak = std::abs(y); peak_index = i; }
  }
  require(peak_index == 15 && std::abs(sum - 1.0f) < 0.003f,
          "oversampling FIR peak is at the reported latency with unity DC gain");
  const auto modulation = [](float rate) {
    pedal::Ph2Mode swept, fixed;
    swept.Init(); fixed.Init();
    auto p = pedal::mod_fx::ParamSet::make_default();
    p.speed = rate; p.depth = 1.0f; p.mix = 1.0f;
    auto q = p; q.depth = 0.0f;
    swept.Prepare(p); fixed.Prepare(q);
    double sum = 0.0;
    for (int i = 0; i < 960000; ++i) {
      const float x = 0.1f * std::sin(kTwoPi * 900.0 * i / kSampleRate);
      const auto a = swept.Process({x, x}, p), b = fixed.Process({x, x}, q);
      if (i > 480000) sum += (a.left - b.left) * (a.left - b.left);
    }
    swept.Reset(); swept.Prepare(p);
    for (int i = 0; i < 1024; ++i) {
      const auto y = swept.Process({0.0f, 0.0f}, p);
      require(y.left == 0.0f && y.right == 0.0f, "PH-2 reset clears all states");
    }
    return std::sqrt(sum / 480000);
  };
  const auto slow = modulation(1.0f / 14.0f), fast = modulation(5.0f);
  require(slow > 5.0 * fast, "filtered CV narrows fast sweeps, " + fmt(slow / fast));
  auto p = defaults("phaser_ph2"); p["mix"] = 0.0f;
  auto processor = configured(p);
  require(processor.latencyFrames() == 15, "PH-2 reports oversampling latency");
  for (size_t i = 0; i < 64; ++i) {
    const auto y = processor.process({i == 0 ? 1.0f : 0.0f, 0.0f});
    require(y.left == (i == 15 ? 1.0f : 0.0f) && y.right == 0.0f, "dry path is aligned and stereo stays separate");
  }
}

void switchingAndStereoHistory()
{
  pedal::Ph2Mode automated, reference;
  automated.Init(); reference.Init();
  auto p = pedal::mod_fx::ParamSet::make_default();
  p.depth = 0.0f; p.mix = 1.0f; p.p1 = 0.8f;
  automated.Prepare(p); reference.Prepare(p);
  float previous_error = 0.0f, max_error_step = 0.0f;
  for (int i = 0; i < 52000; ++i) {
    if (i == 48000) { p.p2 = 1.0f; automated.Prepare(p); }
    const float x = 0.2f * std::cos(kTwoPi * 100.0 * i / kSampleRate);
    const auto a = automated.Process({x, x}, p), b = reference.Process({x, x}, p);
    const float error = a.left - b.left;
    if (i >= 48000 && i < 50000) max_error_step = std::max(max_error_step, std::abs(error - previous_error));
    previous_error = error;
  }
  require(max_error_step < 0.005f, "mode automation crossfades without a discontinuity");
  // After mono state-sharing, different input channels must keep separate
  // histories even if later input samples happen to become equal again.
  automated.Reset(); automated.Prepare(p);
  for (int i = 0; i < 24000; ++i) {
    const float x = 0.1f * std::cos(kTwoPi * 1000.0 * i / kSampleRate);
    automated.Process({x, 0.0f}, p);
  }
  float left_tail = 0.0f;
  for (int i = 0; i < 2400; ++i) {
    const auto y = automated.Process({0.0f, 0.0f}, p);
    require(y.right == 0.0f, "mono shortcut never copies a diverged left history into the right channel");
    left_tail = std::max(left_tail, std::abs(y.left));
  }
  require(left_tail > 1e-5f, "stereo history test exercises an audible left tail");
}

void spectralAndLevelChecks()
{
  for (float mode : {0.0f, 1.0f}) {
    auto p = defaults("phaser_ph2"); p["depth"] = 0.0f; p["p2"] = mode;
    p["p1"] = 0.8f;
    const auto out = render(p, sine(5000.0, 0.5f, 96000));
    const auto bin = [&](double hz) {
      C value{};
      for (size_t i = 48000; i < out.left.size(); ++i)
        value += static_cast<double>(out.left[i]) * std::polar(1.0, -kTwoPi * hz * i / kSampleRate);
      return std::abs(value) / 24000.0;
    };
    const double fundamental = bin(5000.0), folded_fifth = bin(23000.0);
    std::printf("Mode %.0f 5kHz: fundamental %.6f, folded fifth %.2fdBc\n", mode + 1, fundamental, db(folded_fifth / fundamental));
    require(fundamental > 1e-6 && folded_fifth < 0.01 * fundamental,
            "PH-2 5kHz folded fifth stays below -40dBc at nominal maximum input");
    p["depth"] = 0.9f; p["speed"] = 0.15f;
    const auto guitar = render(p, guitarPhrase());
    float peak = 0.0f;
    for (float y : guitar.left) peak = std::max(peak, std::abs(y));
    std::printf("Mode %.0f guitar: gain %.2fdB, peak %.4f\n", mode + 1, levelDb(p), peak);
    require(peak < 0.95f, "nominal guitar does not hit digital clipping");
  }
}

void nonlinearStress()
{
  auto p = pedal::mod_fx::ParamSet::make_default();
  p.mix = 1.0f;
  pedal::Ph2Mode mode;
  mode.Init();
  uint32_t random = 1;
  float peak = 0.0f;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 240000; ++i) {
    if (i % 12000 == 0) {
      const int choice = i / 12000;
      p.p2 = choice % 2; p.p4 = (choice / 2) % 2;
      p.tone = (choice % 3) * 0.5f; p.depth = choice % 2;
      p.speed = choice % 2 ? 10.0f : 1.0f / 14.0f; p.p1 = 1.0f; p.p3 = 1.0f;
      mode.Prepare(p);
    }
    random = random * 1664525U + 1013904223U;
    const float x = static_cast<int32_t>(random) * (4.0f / 2147483648.0f);
    const auto y = mode.Process({x, -x}, p);
    peak = std::max({peak, std::abs(y.left), std::abs(y.right)});
    require(std::isfinite(y.left) && std::isfinite(y.right) && peak < 3.5f, "overload and mode automation stay finite/bounded");
  }
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("PH-2 stereo stress: %.3fs for 5s audio, peak %.4f, solver residual %.8f\n", elapsed, peak, mode.SolverResidual());
  require(mode.SolverResidual() < 2e-5f, "nonlinear feedback solver converges under overload");
  for (int i = 0; i < 480000; ++i) {
    const auto y = mode.Process({0.0f, 0.0f}, p);
    if (i > 479000) require(std::abs(y.left) < 1e-6f && std::abs(y.right) < 1e-6f, "silence does not self-oscillate or retain DC");
  }
}

void writeWav(const std::filesystem::path& path, const Render& audio)
{
  std::ofstream file(path, std::ios::binary);
  const auto word = [&](uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) file.put(static_cast<char>(value >> (8 * i)));
  };
  const uint32_t size = static_cast<uint32_t>(audio.left.size() * 8);
  file.write("RIFF", 4); word(size + 36, 4); file.write("WAVEfmt ", 8);
  word(16, 4); word(3, 2); word(2, 2); word(48000, 4); word(384000, 4);
  word(8, 2); word(32, 2); file.write("data", 4); word(size, 4);
  for (size_t i = 0; i < audio.left.size(); ++i) for (float value : {audio.left[i], audio.right[i]}) {
    uint32_t bits; std::memcpy(&bits, &value, sizeof bits); word(bits, 4);
  }
  require(file.good(), "write review audio");
}

void renderReview(const std::filesystem::path& directory)
{
  std::filesystem::create_directories(directory);
  std::ofstream response(directory / "response.csv");
  response << "mode,resonance,hz,gain_db\n";
  for (int mode : {0, 1}) for (float res : {0.0f, 0.8f}) {
    auto p = defaults("phaser_ph2"); p["depth"] = 0.0f; p["p2"] = mode; p["p1"] = res;
    for (int j = 0; j < 24; ++j) {
      const double hz = 40.0 * std::pow(300.0, j / 23.0);
      const auto out = render(p, sine(hz, 0.1f, 48000));
      double sum = 0.0;
      for (size_t i = 24000; i < out.left.size(); ++i) sum += out.left[i] * out.left[i];
      response << mode + 1 << ',' << res << ',' << hz << ',' << db(std::sqrt(sum / 24000.0) / (0.1 / std::sqrt(2.0))) << '\n';
    }
  }
  response.close();
  auto phrase = guitarPhrase(); phrase.insert(phrase.end(), 96000, 0.0f);
  writeWav(directory / "input.wav", {phrase, phrase});
  for (int mode : {0, 1}) {
    auto p = defaults("phaser_ph2"); p["p2"] = mode; p["p1"] = 0.7f;
    p["speed"] = 0.15f; p["depth"] = 0.9f;
    const auto start = std::chrono::steady_clock::now();
    const auto out = render(p, phrase);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    writeWav(directory / ("mode-" + std::to_string(mode + 1) + ".wav"), out);
    const double peak = *std::max_element(out.left.begin(), out.left.end(), [](float a,float b) {return std::abs(a)<std::abs(b);});
    std::printf("Mode %d guitar: %.3fs for %.1fs mono audio, RMS %.2fdBFS, peak %.4f\n",
                mode + 1, elapsed, phrase.size() / 48000.0, rmsDb(out), std::abs(peak));
  }
}
}

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--render") {
    renderReview(argv[2]); return 0;
  }
  const std::pair<const char*, void(*)()> checks[] = {
    {"circuit response", circuitResponse}, {"compander dynamics", companderDynamics},
    {"sweep/reset/latency", sweepAndReset}, {"nonlinear stress", nonlinearStress},
    {"switching and stereo history", switchingAndStereoHistory},
    {"spectral and level checks", spectralAndLevelChecks},
  };
  int failures = 0;
  for (const auto& [name, check] : checks) try { check(); }
  catch (const std::exception& e) { std::fprintf(stderr, "FAIL %s: %s\n", name, e.what()); ++failures; }
  return failures ? 1 : 0;
}
