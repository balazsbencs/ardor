#include "daisyfx/pog3/PolyphonicPitchBank.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
static_assert(!std::is_copy_constructible_v<ardor::pog3::PitchFrame>);
static_assert(!std::is_copy_constructible_v<ardor::pog3::PolyphonicPitchBank>);
constexpr double pi = 3.14159265358979323846;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void magnitudeRange() {
  using namespace ardor::pog3;
  auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(64, 8));
  PitchFrame frame;
  frame.prepare(plan);
  std::vector<std::complex<float>> spectrum(64);
  std::size_t checked = 0;
  const auto check = [&](float real, float imaginary) {
    // Exercise the actual frame/peak path against the independent library norm.
    spectrum[16] = {real, imaginary};
    const float expected = std::abs(spectrum[16]);
    frame.reset();
    frame.update(spectrum);
    if (!std::isfinite(expected)) {
      require(frame.spectrum().empty(), "unrepresentable peak magnitude rejects frame");
    } else {
      require(!frame.spectrum().empty(), "finite magnitude retains frame across float range");
      if (expected <= 1e-7f) require(frame.regions().empty(), "subthreshold magnitude remains silent");
      else {
        require(frame.regions().size() == 1 && frame.regions()[0].bin == 16,
                "isolated peak survives magnitude calculation");
        const auto a = std::bit_cast<std::uint32_t>(expected);
        const auto b = std::bit_cast<std::uint32_t>(frame.regions()[0].magnitude);
        require((a > b ? a - b : b - a) <= 1, "peak magnitude agrees within one float ulp");
        frame.update(spectrum);
        require(frame.regions().size() == 1 && frame.regions()[0].frequencyBins == 16,
                "previous-frame magnitude retains stationary phase tracking");
      }
    }
    ++checked;
  };
  for (const float value : {0.0f, std::numeric_limits<float>::denorm_min(), 1e-30f,
                           1e-7f, .2f, 1e20f, std::numeric_limits<float>::max()}) {
    check(value, 0); check(value, -value);
  }
  std::mt19937 generator(0x504f4733);
  for (unsigned i = 0; i < 65536; ++i) {
    const float real = std::bit_cast<float>(static_cast<std::uint32_t>(generator()));
    const float imaginary = std::bit_cast<float>(static_cast<std::uint32_t>(generator()));
    if (std::isfinite(real) && std::isfinite(imaginary)) check(real, imaginary);
  }
  std::cout << "Frame magnitude range: " << checked << " pairs passed\n";
}
double frequency(const std::vector<float>& signal, std::size_t skip) {
  double first = 0, last = 0;
  std::size_t count = 0;
  for (std::size_t i = skip + 1; i < signal.size(); ++i) if (signal[i - 1] < 0 && signal[i] >= 0) {
    const double t = i - 1 + signal[i - 1] / (signal[i - 1] - signal[i]);
    if (!count) first = t;
    last = t;
    ++count;
  }
  require(count > 10, "enough cycles for tuning measurement");
  return (count - 1) * 48000 / (last - first);
}
struct Metrics { double cents = 0, spurDb = 0, amplitude = 0, originalDb = -300; };
Metrics measure(const std::vector<float>& signal, double expected, double original = 0) {
  constexpr std::size_t n = 65536;
  require(signal.size() > n + 48000, "settled analysis window");
  ardor::RealtimeFft fft;
  fft.prepare(n);
  std::vector<std::complex<float>> spectrum(n);
  double energy = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const float x = signal[signal.size() - n + i];
    spectrum[i] = {static_cast<float>(x * (.5 - .5 * std::cos(2 * pi * i / n))), 0};
    energy += static_cast<double>(x) * x;
  }
  fft.transform(spectrum, false);
  const double center = expected * n / 48000;
  double main = 0, spur = 0, leakage = 0;
  for (std::size_t i = 1; i < n / 2; ++i) {
    const double magnitude = std::abs(spectrum[i]);
    if (std::fabs(static_cast<double>(i) - center) <= 8) main = std::max(main, magnitude);
    else if (magnitude >= std::abs(spectrum[i - 1]) && magnitude >= std::abs(spectrum[i + 1]))
      spur = std::max(spur, magnitude);
    if (original > 0 && std::fabs(static_cast<double>(i) - original * n / 48000) <= 1.5)
      leakage = std::max(leakage, magnitude);
  }
  require(main > 1e-3, "expected output partial survives");
  return {1200 * std::log2(frequency(signal, 48000) / expected),
          20 * std::log10(std::max(spur / main, 1e-15)), std::sqrt(2 * energy / n),
          20 * std::log10(std::max(leakage / main, 1e-15))};
}

double amplitudeAt(const std::vector<float>& signal, double frequencyHz) {
  const std::size_t n = std::min<std::size_t>(96000, signal.size());
  std::complex<double> sum{};
  double windowSum = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double window = .5 - .5 * std::cos(2 * pi * i / n);
    const double angle = 2 * pi * frequencyHz * i / 48000;
    sum += static_cast<double>(signal[signal.size() - n + i]) * window
      * std::complex<double>{std::cos(angle), -std::sin(angle)};
    windowSum += window;
  }
  return 2 * std::abs(sum) / windowSum;
}

void polyphony() {
  using namespace ardor::pog3;
  for (const bool focus : {false, true}) {
    PolyphonicPitchBank bank;
    bank.setFocus(focus); bank.prepare();
    std::array<std::vector<float>, 5> output;
    for (auto& signal : output) signal.resize(3 * 48000);
    for (std::size_t i = 0; i < output[0].size(); ++i) {
      float x = 0;
      for (double f : {196.0, 329.628, 493.883, 733.31}) x += .04 * std::sin(2 * pi * f * i / 48000);
      const auto voices = bank.process({x, -.7f * x});
      for (std::size_t v = 1; v < 6; ++v) output[v - 1][i] = voices[v].left;
    }
    for (std::size_t v = 1; v < 6; ++v) for (double f : {196.0, 329.628, 493.883, 733.31}) {
      const double amplitude = amplitudeAt(output[v - 1], f * std::exp2(kVoiceSemitones[v] / 12));
      const double delta = 20 * std::log10(amplitude / .04);
      std::cout << "Resolved chord Focus=" << focus << " voice=" << v << " input=" << f << " level=" << delta << " dB\n";
      require(std::fabs(delta) < 3, "all resolved chord/inharmonic partials retained within 3 dB");
    }
  }
}

void aliasing() {
  using namespace ardor::pog3;
  for (double f : {7001.3, 10003.7, 17003.2}) {
    for (const bool focus : {false, true}) {
      PolyphonicPitchBank bank;
      bank.setFocus(focus); bank.prepare();
      double energy = 0;
      constexpr std::size_t length = 2 * 48000;
      for (std::size_t i = 0; i < length; ++i) {
        const float x = .2 * std::sin(2 * pi * f * i / 48000);
        const auto voices = bank.process({x, x});
        if (i >= 48000) energy += static_cast<double>(voices[5].left) * voices[5].left;
      }
      const double relative = 10 * std::log10(std::max(energy / 48000 / .02, 1e-30));
      std::cout << "Alias rejection input=" << f << " Focus=" << focus << ": " << relative << " dB\n";
      require(relative < -50, "out-of-band +24 region does not fold into audible bins");
    }
  }
}

void lifecycleAndFocus() {
  using namespace ardor::pog3;
  PolyphonicPitchBank unprepared;
  const auto silent = unprepared.process({1, 1});
  for (const auto voice : silent) require(voice.left == 0 && voice.right == 0, "unprepared bank is silent");
  require(!unprepared.setWarp(std::numeric_limits<float>::infinity()), "nonfinite Warp target rejected");
  PolyphonicPitchBank off, on, moving;
  on.setFocus(true);
  off.prepare(); on.prepare(); moving.prepare();
  for (std::size_t i = 0; i < 24000; ++i) {
    if (i == 7000 || i == 8000 || i == 13000) moving.setFocus(true);
    if (i == 7400 || i == 10000 || i == 16000) moving.setFocus(false);
    const float x = .2 * std::sin(2 * pi * 329.628 * i / 48000);
    const auto a = off.process({x, .3f * x}), b = on.process({x, .3f * x}), y = moving.process({x, .3f * x});
    const float position = moving.focusPosition();
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
      if (voice < 4) require(y[voice].left == a[voice].left && y[voice].right == b[voice].right,
                            "Focus never changes lower or unison voice histories");
      else {
        require(std::fabs(y[voice].left - (position * b[voice].left + (1 - position) * a[voice].left)) < 1e-6,
                "Focus reversals keep both upper paths warm and coherent");
      }
    }
  }
  require(moving.focusPosition() == 0, "latest reversed Focus target settles exactly");
  require(moving.setWarp(.5f), "valid Warp publication");
  moving.setFocus(true); moving.reset(); moving.reset();
  require(moving.focusPosition() == 1, "reset retains and reseeds Focus target");
  for (std::size_t i = 0; i < 8000; ++i) {
    const auto voices = moving.process({0, 0});
    for (const auto voice : voices) require(voice.left == 0 && voice.right == 0, "reset drains every voice and low-band state");
  }
  require(moving.healthy(), "reset state remains healthy");
}

void trackContinuity() {
  using namespace ardor::pog3;
  auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(2048, 256));
  SpectralAnalysis analysis;
  PitchFrame frame;
  analysis.prepare(plan->spectral); frame.prepare(plan);
  double phase = 0;
  std::uint64_t identity = 0;
  std::size_t firstBin = 0, lastBin = 0;
  for (std::size_t i = 0; i < 48000; ++i) {
    const double f = 196 + 100.0 * i / 48000;
    phase += 2 * pi * f / 48000;
    if (!analysis.push(.2 * std::sin(phase))) continue;
    frame.update(analysis.spectrum());
    if (i < 8000) continue;
    const auto regions = frame.regions();
    const auto dominant = std::max_element(regions.begin(), regions.end(), [](const auto& a, const auto& b) {
      return a.magnitude < b.magnitude;
    });
    require(dominant != regions.end(), "chirp partial retained");
    if (!identity) { identity = dominant->generation; firstBin = dominant->bin; }
    require(dominant->generation == identity, "track identity persists across moving FFT bins");
    lastBin = dominant->bin;
  }
  require(lastBin > firstBin + 2, "continuity fixture crosses multiple bin boundaries");
}

void stagedIdentityAndPartitioning() {
  using namespace ardor::pog3;
  std::vector<float> input(12013);
  std::mt19937 random(3);
  std::uniform_real_distribution<float> noise(-.2f, .2f);
  for (auto& sample : input) sample = noise(random);
  PolyphonicPitchBank bank;
  bank.setFocus(true);
  require(bank.setWarp(0), "Warp heel retained for reset");
  bank.prepare();
  for (std::size_t i = 0; i < input.size() + PolyphonicPitchBank::kLongDelay; ++i) {
    const float x = i < input.size() ? input[i] : 0;
    const auto voices = bank.process({x, -.3f * x});
    const float expected = i >= PolyphonicPitchBank::kLongDelay ? input[i - PolyphonicPitchBank::kLongDelay] : 0;
    for (const auto voice : voices) {
      require(std::fabs(voice.left - expected) < 1e-6f, "all unison jobs have exactly N+H staged delay");
      require(std::fabs(voice.right + .3f * expected) < 1e-6f, "staging retains independent stereo");
    }
  }
  require(bank.deadlineMisses() == 0 && bank.healthy(), "all jobs finish before their windows become audible");
  const auto render = [&](const std::vector<std::size_t>& chunks) {
    PolyphonicPitchBank processor;
    processor.prepare();
    std::vector<PitchVoices> output(input.size());
    std::size_t pos = 0, chunk = 0;
    while (pos < input.size()) {
      const auto end = std::min(pos + chunks[chunk++ % chunks.size()], input.size());
      for (; pos < end; ++pos) {
        if (pos % 480 == 0) require(processor.setWarp((pos / 480) % 128 / 127.0f), "timestamped Warp update");
        if (pos == 5000 || pos == 7000) processor.setFocus(true);
        if (pos == 5500 || pos == 9000) processor.setFocus(false);
        output[pos] = processor.process({input[pos], .2f * input[pos]});
      }
    }
    require(processor.healthy() && processor.deadlineMisses() == 0, "partitioning never misses a job deadline");
    return output;
  };
  const auto contiguous = render({input.size()}), split = render({1, 17, 48, 64, 128, 256});
  for (std::size_t i = 0; i < split.size(); ++i) for (std::size_t v = 0; v < kVoiceCount; ++v)
    require(split[i][v].left == contiguous[i][v].left && split[i][v].right == contiguous[i][v].right,
            "staged pitch/automation output is invariant to callback partitioning");
}

void warpAndOverload() {
  using namespace ardor::pog3;
  PolyphonicPitchBank bank;
  require(bank.setWarp(.5f), "Warp midpoint publication");
  bank.prepare();
  std::array<std::vector<float>, 5> output;
  for (auto& signal : output) signal.resize(3 * 48000);
  for (const bool focus : {false, true}) {
    bank.setFocus(focus); bank.reset();
    for (std::size_t i = 0; i < output[0].size(); ++i) {
      const float x = .2 * std::sin(2 * pi * 196 * i / 48000);
      const auto voices = bank.process({x, 0});
      for (std::size_t v = 1; v < 6; ++v) {
        output[v - 1][i] = voices[v].left;
        require(voices[v].right == 0, "right-only silence is never reconstructed from left input");
      }
    }
    for (std::size_t v = 1; v < 6; ++v) {
      const auto m = measure(output[v - 1], 196 * std::exp2(warpSemitones(v, .5f, focus) / 12));
      require(std::fabs(m.cents) < 3 && m.spurDb < -45, "continuous fractional Warp intervals retain pitch and quality");
    }
  }
  bank.reset();
  for (std::size_t i = 0; i < 48000; ++i) {
    if (i % 480 == 0) { bank.setFocus((i / 480) % 2); require(bank.setWarp((i / 480) % 128 / 127.0f), "rapid Warp"); }
    const float x = i < 20000 ? 12 * std::sin(2 * pi * 196 * i / 48000) : 0;
    const auto voices = bank.process({x, -.7f * x});
    for (const auto voice : voices) {
      require(std::isfinite(voice.left) && std::isfinite(voice.right) && std::fabs(voice.left) < 100,
              "floating headroom and finite overload state");
      if (i > 30000) require(std::fabs(voice.left) < 1e-6 && std::fabs(voice.right) < 1e-6,
                             "overload and automation recover to silence");
    }
  }
  require(bank.healthy() && bank.deadlineMisses() == 0, "overload keeps scheduler and spectra valid");
}

// Separate admission test. Failure is an explicit release blocker, not a
// relaxed threshold in the core tests or permission to expose a partial block.
bool stress() {
  using namespace ardor::pog3;
  bool ordinaryChords = true;
  for (const bool harmonics : {false, true}) for (const bool focus : {false, true}) {
    PolyphonicPitchBank bank;
    bank.setFocus(focus); bank.prepare();
    std::array<std::vector<float>, 5> output;
    for (auto& signal : output) signal.resize(3 * 48000);
    for (std::size_t i = 0; i < output[0].size(); ++i) {
      float x = 0;
      for (double f : {130.8128, 164.8138, 196.0}) {
        x += .04 * std::sin(2 * pi * f * i / 48000);
        if (harmonics) for (const auto& [multiple, level] : {std::pair{2, .02}, {3, .01}, {5, .004}})
          x += level * std::sin(2 * pi * f * multiple * i / 48000);
      }
      const auto voices = bank.process({x, -.7f * x});
      for (std::size_t v = 1; v < 6; ++v) output[v - 1][i] = voices[v].left;
    }
    for (std::size_t v = 1; v < 6; ++v) for (double f : {130.8128, 164.8138, 196.0}) {
      const double delta = 20 * std::log10(std::max(amplitudeAt(output[v - 1], f * std::exp2(kVoiceSemitones[v] / 12)) / .04, 1e-15));
      std::cout << "Low chord harmonics=" << harmonics << " Focus=" << focus << " voice=" << v << " input=" << f
                << " level=" << delta << " dB gate=" << (std::fabs(delta) < 3 ? "PASS" : "FAIL") << '\n';
      ordinaryChords &= std::fabs(delta) < 3;
    }
  }
  std::cout << "Ordinary low-chord admission: " << (ordinaryChords ? "PASS" : "FAIL — pitch refinement required before public integration") << '\n';
  return ordinaryChords;
}

void resolutionStress() {
  using namespace ardor::pog3;
  PolyphonicPitchBank bank;
  bank.setFocus(true); bank.prepare();
  std::vector<float> output(3 * 48000);
  for (std::size_t i = 0; i < output.size(); ++i) {
    float x = 0;
    for (double f : {82.4069, 87.3071}) x += .04 * std::sin(2 * pi * f * i / 48000);
    output[i] = bank.process({x, x})[1].left;
  }
  for (double f : {82.4069, 87.3071})
    std::cout << "Unresolved low pair input=" << f << " shifted partial level="
              << 20 * std::log10(std::max(amplitudeAt(output, f * .25) / .04, 1e-15)) << " dB\n";
  std::cout << "Resolution diagnostic only: 4.9 Hz input separation is below the 11.72 Hz low-band bin spacing.\n";
}

void envelopeLatency() {
  using namespace ardor::pog3;
  for (double f : {82.4, 659.3}) for (const bool focus : {false, true}) {
    PolyphonicPitchBank bank;
    bank.setFocus(focus); bank.prepare();
    double inputEnergy = 0, inputTime = 0, outputEnergy = 0, outputTime = 0;
    for (std::size_t i = 0; i < 36000; ++i) {
      const float x = i >= 6000 && i < 18000 ? .2 * std::sin(2 * pi * f * i / 48000) : 0;
      const auto y = bank.process({x, x})[4].left;
      inputEnergy += static_cast<double>(x) * x; inputTime += static_cast<double>(x) * x * i;
      outputEnergy += static_cast<double>(y) * y; outputTime += static_cast<double>(y) * y * i;
    }
    const double milliseconds = (outputTime / outputEnergy - inputTime / inputEnergy) / 48;
    std::cout << "Envelope energy centroid input=" << f << " Focus=" << focus << " delay=" << milliseconds << " ms\n";
    require(std::isfinite(milliseconds) && milliseconds > 10 && milliseconds < 90,
            "bounded measured octave-envelope latency");
  }
}

void warpTimeInvariance() {
  using namespace ardor::pog3;
  const auto render = [](std::size_t warmup) {
    PolyphonicPitchBank bank;
    bank.setFocus(true);
    require(bank.setWarp(0), "initial unison Warp target");
    bank.prepare();
    for (std::size_t i = 0; i < warmup; ++i)
      (void)bank.process({static_cast<float>(.2 * std::sin(2 * pi * 200 * i / 48000)), 0});
    require(bank.setWarp(.01f), "small Warp gesture");
    std::vector<PitchVoices> output(4096);
    for (std::size_t i = 0; i < output.size(); ++i)
      output[i] = bank.process({static_cast<float>(.2 * std::sin(2 * pi * 200 * (i + warmup) / 48000)), 0});
    require(bank.healthy(), "Warp onset remains healthy");
    return output;
  };
  // 7680 is a multiple of the tone's period and every analysis hop. Both
  // gestures therefore see exactly the same steady signal and frame alignment.
  const auto early = render(7680), later = render(15360);
  float maximum = 0;
  for (std::size_t i = 0; i < early.size(); ++i) for (std::size_t v = 1; v < kVoiceCount; ++v)
    maximum = std::max(maximum, std::fabs(early[i][v].left - later[i][v].left));
  std::cout << "Warp onset elapsed-time invariance maximum error=" << maximum << '\n';
  require(maximum < 1e-5, "starting Warp does not jump phase according to elapsed runtime");
}

void unity() {
  using namespace ardor::pog3;
  auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(2048, 256));
  SpectralAnalysis analysis;
  PitchFrame frame;
  PitchRenderer renderer;
  analysis.prepare(plan->spectral); frame.prepare(plan); renderer.prepare(plan);
  std::mt19937 generator(42);
  std::uniform_real_distribution<float> noise(-.2f, .2f);
  std::vector<float> input(16003);
  for (auto& x : input) x = noise(generator);
  double residual = 0, reference = 0;
  for (std::size_t i = 0; i < input.size() + 2048; ++i) {
    const float y = renderer.pop(), expected = i >= 2048 ? input[i - 2048] : 0;
    const double error = static_cast<double>(y) - expected;
    residual += error * error;
    reference += static_cast<double>(expected) * expected;
    if (analysis.push(i < input.size() ? input[i] : 0)) {
      frame.update(analysis.spectrum());
      require(renderer.render(frame, 0), "unity frame accepted");
    }
  }
  const double db = 10 * std::log10(std::max(residual / reference, 1e-30));
  std::cout << "General pitch identity: " << db << " dB\n";
  require(db < -90, "general region renderer passes spectral identity");
}

void tones() {
  using namespace ardor::pog3;
  bool passed = true;
  for (const bool focus : {false, true}) {
    PolyphonicPitchBank bank;
    bank.setFocus(focus); bank.prepare();
    constexpr std::size_t length = 3 * 48000;
    std::array<std::vector<float>, 5> outputs;
    for (auto& output : outputs) output.resize(length);
    for (const double inputFrequency : {65.4, 82.4, 110.0, 146.8, 196.0, 329.6, 659.3, 196.257, 199.21875, 210.9375}) {
      bank.reset();
      for (std::size_t i = 0; i < length; ++i) {
        const float x = .2 * std::sin(2 * pi * inputFrequency * i / 48000);
        const auto y = bank.process({x, -x});
        for (std::size_t v = 1; v < kVoiceCount; ++v) {
          outputs[v - 1][i] = y[v].left;
          require(std::isfinite(y[v].left) && std::isfinite(y[v].right), "finite bank output");
          require(std::fabs(y[v].left + y[v].right) < .0001f, "anti-phase stereo remains independent");
        }
      }
      require(bank.healthy(), "all spectral frames accepted");
      for (std::size_t v = 1; v < kVoiceCount; ++v) {
        const auto m = measure(outputs[v - 1], inputFrequency * std::exp2(kVoiceSemitones[v] / 12), inputFrequency);
        std::cout << "Focus=" << focus << " input=" << inputFrequency << " voice=" << v << " cents=" << m.cents
                  << " spur=" << m.spurDb << " dBc amplitude=" << m.amplitude << " original=" << m.originalDb << " dBc\n";
        passed &= std::fabs(m.cents) < 3 && m.spurDb < -45 && m.amplitude > .15 && m.amplitude < .25 && m.originalDb < -60;
      }
    }
  }
  require(passed, "spectral tone matrix tuning/spur/gain gates");
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--magnitude-range") { magnitudeRange(); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--stress") return stress() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--resolution-stress") { resolutionStress(); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--latency") { envelopeLatency(); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--warp-onset") { warpTimeInvariance(); return 0; }
    require(argc == 1, "usage: pedal-pog3-pitch-quality [--stress|--resolution-stress|--latency|--warp-onset|--magnitude-range]");
    magnitudeRange();
    unity(); tones(); polyphony(); aliasing(); lifecycleAndFocus(); trackContinuity();
    stagedIdentityAndPartitioning(); warpAndOverload(); envelopeLatency(); warpTimeInvariance();
    std::cout << "Spectral pitch, Focus, staged deadlines, Warp and overload gates passed\n";
    return 0;
  }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
