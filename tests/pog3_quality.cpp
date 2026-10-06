#include "daisyfx/pog3/SpectralFrameStream.h"
#include "daisyfx/pog3/PolyphonicPitchBank.h"
#include "pog3_granular_reference.h"
#include "pog3_artifacts.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
constexpr double kTwoPi = 6.2831853071795864769;
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
std::vector<float> identity(const std::vector<float>& input, std::size_t n, std::size_t hop,
                            const std::vector<std::size_t>& chunks) {
  using namespace ardor::pog3;
  auto plan = std::make_shared<SpectralPlan>(n, hop);
  SpectralAnalysis analysis;
  SpectralSynthesis synthesis;
  analysis.prepare(plan);
  synthesis.prepare(plan);
  std::vector<float> output(input.size() + n, 0);
  std::size_t pos = 0, chunk = 0;
  while (pos < output.size()) {
    const auto end = std::min(pos + chunks[chunk++ % chunks.size()], output.size());
    for (; pos < end; ++pos) {
      output[pos] = synthesis.pop();
      const bool ready = analysis.push(pos < input.size() ? input[pos] : 0);
      require(ready == ((pos + 1) % hop == 0), "frame completion follows the absolute sample timeline");
      require(analysis.frameCount() == (pos + 1) / hop, "analysis frame count matches hop timestamps");
      if (ready)
        require(synthesis.addFrame(analysis.spectrum()), "valid analysis frame accepted");
    }
  }
  return output;
}

void spectralIdentity() {
  std::mt19937 random(0x504f4733);
  std::uniform_real_distribution<float> distribution(-.3f, .3f);
  std::vector<float> input(19013);
  for (auto& value : input) value = distribution(random);
  for (const auto n : {1024U, 2048U}) {
    for (const auto hop : {n / 8, n / 4, n / 2}) {
      const auto output = identity(input, n, hop, {1, 17, 48, 64, 128, 256});
      const auto contiguous = identity(input, n, hop, {input.size() + n});
      require(output == contiguous, "arbitrary callback partition must not change the stream");
      double reference = 0, error = 0;
      for (std::size_t i = 0; i < output.size(); ++i) {
        const float expected = i >= n ? input[i - n] : 0;
        const double delta = static_cast<double>(output[i]) - expected;
        error += delta * delta;
        reference += static_cast<double>(expected) * expected;
        require(std::fabs(delta) < 1e-6, "identity including startup/drain must match N-delayed input");
      }
      const double residualDb = 10 * std::log10(std::max(error / reference, 1e-30));
      require(residualDb < -90, "spectral identity residual below -90 dB");
      std::cout << "WOLA N=" << n << " H=" << hop << " residual=" << residualDb << " dB\n";
    }
    std::vector<float> impulse(n * 2, 0);
    impulse[0] = .5f;
    const auto output = identity(impulse, n, n / 8, {64});
    require(std::distance(output.begin(), std::max_element(output.begin(), output.end())) == n,
            "measured impulse delay is exactly N");
  }
}

void preparedTransform() {
  using namespace ardor::pog3;
  std::mt19937 random(0x46544654);
  std::uniform_real_distribution<float> uniform(-.3f, .3f);
  std::size_t compared = 0;
  // FFTW changes butterfly ordering: production sizes have a numerical
  // accuracy contract. Other sizes retain the shared FFT bit-for-bit.
  for (std::size_t n = 32; n <= 32768; n *= 2) {
    SpectralPlan plan(n, n / 8);
    ardor::RealtimeFft reference; reference.prepare(n);
    std::vector<std::complex<float>> source(n), expected(n), actual(n);
    for (unsigned fixture = 0; fixture < 14; ++fixture) {
      for (std::size_t i = 0; i < n; ++i) {
        const float sign = i & 1 ? -1 : 1;
        switch (fixture) {
          case 0: source[i] = {}; break;
          case 1: source[i] = {sign * 0.0f, -sign * 0.0f}; break;
          case 2: source[i] = i == 0 ? std::complex<float>{.5f, -.25f} : std::complex<float>{}; break;
          case 3: source[i] = {.125f, -.0625f}; break;
          case 4: source[i] = {.25f * sign, -.125f * sign}; break;
          case 5: source[i] = {uniform(random), uniform(random)}; break;
          case 6: source[i] = {uniform(random), 0}; break;
          case 7: source[i] = {uniform(random), uniform(random)}; break;
          case 8: source[i] = {sign * std::ldexp(1.0f, -140), -sign * std::ldexp(1.0f, -135)}; break;
          case 9: source[i] = {std::ldexp(uniform(random), static_cast<int>(i % 121) - 60),
                              std::ldexp(uniform(random), 60 - static_cast<int>(i % 121))}; break;
          case 10: source[i] = {std::numeric_limits<float>::max(), 0}; break;
          case 11: source[i] = i == n / 2 ? std::complex<float>{std::numeric_limits<float>::max(), 0}
                                        : std::complex<float>{}; break;
          case 12: source[i] = i == n - 1 ? std::complex<float>{std::numeric_limits<float>::quiet_NaN(), 0}
                                        : std::complex<float>{}; break;
          default: source[i] = i == n / 3 ? std::complex<float>{0, std::numeric_limits<float>::infinity()}
                                         : std::complex<float>{}; break;
        }
      }
      if (fixture == 7) {
        source[0] = {source[0].real(), 0}; source[n / 2] = {source[n / 2].real(), 0};
        for (std::size_t i = 1; i < n / 2; ++i) source[n - i] = std::conj(source[i]);
      }
      for (bool inverse : {false, true}) {
        expected = actual = source;
        reference.transform(expected, inverse); plan.transform(actual, inverse);
        const bool fftw = n == 1024 || n == 2048 || n == 4096;
        if (!fftw) {
          bool matches = true;
          for (std::size_t i = 0; i < n; ++i) {
            const auto same = [](float a, float b) {
              if (std::isnan(a)) return std::isnan(b);
              return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
            };
            matches &= same(expected[i].real(), actual[i].real()) && same(expected[i].imag(), actual[i].imag());
          }
          require(matches, "fallback FFT retains the shared implementation bits/classification");
        } else if (fixture < 10 || fixture == 11) {
          double energy = 0, error = 0;
          for (std::size_t i = 0; i < n; ++i) {
            const std::complex<double> a(actual[i].real(), actual[i].imag());
            const std::complex<double> b(expected[i].real(), expected[i].imag());
            require(std::isfinite(a.real()) && std::isfinite(a.imag()), "finite FFT fixture stays finite");
            energy += std::norm(b); error += std::norm(a - b);
          }
          const double quantum = 32.0 * std::numeric_limits<float>::denorm_min();
          require(error <= energy * 1e-11 + n * quantum * quantum,
                  "FFTW relative L2 error below -110 dB (absolute rounding floor for subnormals)");
          if (fixture == 2) {
            // Impulse independently checks DC, sign and the single 1/N gain.
            const auto dc = inverse ? source[0] / static_cast<float>(n) : source[0];
            require(actual[0] == dc, "FFT impulse DC has the exact expected normalization");
          }
        } else {
          // Overflow/nonfinite classification depends on butterfly ordering.
          // Audio rejection/recovery is independently tested below through
          // synthesis, including preservation of already pending WOLA output.
          require(std::any_of(actual.begin(), actual.end(), [](auto value) {
            return !std::isfinite(value.real()) || !std::isfinite(value.imag());
          }), "overflow/nonfinite fixture is observable to frame rejection");
        }
        compared += n;
      }
    }
  }
  std::cout << "Prepared FFT: " << compared << " complex bins satisfy FFT accuracy/fallback contracts\n";
}

void sharedPlanExecution() {
  using namespace ardor::pog3;
  for (const std::size_t n : {1024U, 2048U, 4096U}) {
    auto plan = std::make_shared<const SpectralPlan>(n, n / 8);
    std::vector<std::complex<float>> input(n);
    for (std::size_t i = 0; i < n; ++i)
      input[i] = {static_cast<float>(.2 * std::sin(kTwoPi * 17 * i / n)), .03f};
    auto expected = input;
    plan->transform(expected, false);
    std::array<std::vector<std::complex<float>>, 2> results{input, input};
    std::array<std::thread, 2> workers;
    for (std::size_t channel = 0; channel < workers.size(); ++channel)
      workers[channel] = std::thread([&, channel, owner = plan] {
        for (unsigned repeat = 0; repeat < 64; ++repeat) {
          results[channel] = input;
          owner->transform(results[channel], false);
        }
      });
    for (auto& worker : workers) worker.join();
    require(results[0] == expected && results[1] == expected,
            "shared immutable FFTW plan executes concurrently on independent scratch");
    auto recovered = expected;
    plan->transform(recovered, true);
    double energy = 0, error = 0;
    for (std::size_t i = 0; i < n; ++i) {
      energy += std::norm(std::complex<double>(input[i]));
      error += std::norm(std::complex<double>(recovered[i]) - std::complex<double>(input[i]));
    }
    require(error < energy * 1e-11, "complex FFTW round trip retains gain and phase below -110 dB");
  }
}

double measuredFrequency(const std::vector<float>& input, std::size_t skip) {
  std::vector<double> crossings;
  for (std::size_t i = skip + 1; i < input.size(); ++i)
    if (input[i - 1] < 0 && input[i] >= 0)
      crossings.push_back(static_cast<double>(i - 1) + input[i - 1] / (input[i - 1] - input[i]));
  require(crossings.size() >= 5, "enough periods to measure suboctave pitch");
  return (crossings.size() - 1) * 48000.0 / (crossings.back() - crossings.front());
}

void granularReference() {
  using namespace ardor::pog3;
  pog3_test::GranularReference reference;
  auto values = defaultValues();
  // Unmodified dry must remain immediate even with all ten shifters warm.
  for (std::size_t voice = 1; voice < 6; ++voice) values[voice + 1] = 0;
  reference.setValues(values);
  const auto dry = reference.process({.25f, -.125f});
  require(dry.left == .25f && dry.right == -.125f, "reference dry identity");
  values[index(Parameter::DryLevel)] = 0;
  constexpr std::size_t frames = 3 * 48000;
  std::vector<float> left(frames), right(frames);
  for (std::size_t voice = 1; voice < kVoiceCount; ++voice) {
    reference.reset();
    for (std::size_t v = 1; v < kVoiceCount; ++v) values[v + 1] = v == voice ? 1 : 0;
    reference.setValues(values);
    for (std::size_t i = 0; i < frames; ++i) {
      const float x = static_cast<float>(.2 * std::sin(kTwoPi * 196.0 * i / 48000.0));
      const auto output = reference.process({x, -x});
      left[i] = output.left;
      right[i] = output.right;
      require(std::isfinite(output.left) && std::fabs(output.left + output.right) < 1e-5,
              "independent reference channels preserve centered anti-phase signal");
    }
    const double expected = 196 * std::exp2(kVoiceSemitones[voice] / 12.0);
    const double cents = 1200 * std::log2(measuredFrequency(left, 48000) / expected);
    require(std::fabs(cents) < 3, "isolated reference voice within 3 cents");
    std::cout << "Granular reference " << kVoiceSemitones[voice] << " st: " << cents << " cents\n";
  }
  reference.reset();
  for (std::size_t v = 1; v < kVoiceCount; ++v) values[v + 1] = 0;
  values[index(Parameter::DryLevel)] = 1;
  values[index(Parameter::DryPan)] = 0;
  reference.setValues(values);
  const auto pan = reference.process({0, .25f});
  require(std::fabs(pan.left - .176776695f) < 1e-6 && pan.right == 0,
          "hard-left folds right-only source to left and exactly mutes right");
  values[index(Parameter::DryPan)] = .5f;
  values[index(Parameter::InputGain)] = .6f; // 2x
  values[index(Parameter::MasterLevel)] = .75f; // 1.5x
  reference.setValues(values);
  const auto gain = reference.process({.1f, .2f});
  require(std::fabs(gain.left - .3f) < 1e-6 && std::fabs(gain.right - .6f) < 1e-6,
          "input gain and final master each apply exactly once");
  values[index(Parameter::MasterLevel)] = 0;
  reference.setValues(values);
  const auto mute = reference.process({.1f, .2f});
  require(mute.left == 0 && mute.right == 0, "master zero is an exact mute");
}

void spectralLifecycle() {
  using namespace ardor::pog3;
  auto plan = std::make_shared<SpectralPlan>(1024, 128);
  SpectralAnalysis analysis;
  SpectralSynthesis synthesis;
  require(!analysis.push(1) && synthesis.pop() == 0, "unprepared stream is silent/safe");
  analysis.prepare(plan);
  synthesis.prepare(plan);
  for (std::size_t i = 0; i < 4096; ++i) {
    (void)synthesis.pop();
    if (analysis.push(.2f)) require(synthesis.addFrame(analysis.spectrum()), "frame accepted");
  }
  analysis.reset(); synthesis.reset(); analysis.reset(); synthesis.reset();
  require(analysis.frameCount() == 0, "reset clears frame timeline");
  for (std::size_t i = 0; i < 4096; ++i) {
    require(synthesis.pop() == 0, "reset clears all OLA/history, including repeated reset");
    if (analysis.push(0)) require(synthesis.addFrame(analysis.spectrum()), "zero frame accepted");
  }
  std::vector<std::complex<float>> invalid(1024);
  invalid.back() = {std::numeric_limits<float>::quiet_NaN(), 0};
  require(!synthesis.addFrame(invalid), "bad frame rejected before OLA mutation");
  require(!synthesis.addFrame(std::span(invalid).first(100)), "wrong-size frame rejected");
  std::fill(invalid.begin(), invalid.end(), std::complex<float>{std::numeric_limits<float>::max(), 0});
  require(!synthesis.addFrame(invalid), "finite spectrum with inverse overflow is rejected");
  for (std::size_t i = 0; i < 2048; ++i) require(synthesis.pop() == 0, "invalid frame does not poison output");
  SpectralSynthesis healthy;
  healthy.prepare(plan);
  std::vector<std::complex<float>> valid(1024);
  valid[0] = {128, 0};
  require(synthesis.addFrame(valid) && healthy.addFrame(valid), "pending valid frame accepted");
  require(!synthesis.addFrame(invalid), "overflow frame rejected with valid pending output");
  for (std::size_t i = 0; i < 2048; ++i)
    require(synthesis.pop() == healthy.pop(), "invalid frame preserves existing healthy OLA exactly");
  for (std::size_t i = 0; i < 2048; ++i) {
    (void)synthesis.pop();
    if (analysis.push(std::numeric_limits<float>::infinity()))
      require(synthesis.addFrame(analysis.spectrum()), "nonfinite input becomes clean zero history");
  }
  for (std::size_t i = 0; i < 2048; ++i) require(synthesis.pop() == 0, "nonfinite input remains silent");
  for (auto [n, h] : {std::pair{0U, 0U}, {16U, 2U}, {65536U, 8192U}, {1023U, 128U},
                      {1024U, 0U}, {1024U, 96U}, {1024U, 1024U}}) {
    bool rejected = false;
    try { SpectralPlan bad(n, h); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid spectral dimensions rejected off callback");
  }
}

void deferredAnalysis() {
  using namespace ardor::pog3;
  for (const std::size_t n : {32U, 1024U, 2048U, 4096U}) {
    for (const std::size_t hop : {std::size_t{2}, n / 8, n / 2}) {
      auto plan = std::make_shared<SpectralPlan>(n, hop);
      SpectralAnalysis immediate, deferred;
      immediate.prepare(plan); deferred.prepare(plan, true);
      std::uint32_t random = 0x44454652;
      // Multiple history wraps, including sanitization and a drain. Compare
      // complete FFT bins against an independent immediate analysis, not an
      // approximation of the transform or a tolerance that could hide skew.
      for (std::size_t i = 0; i < 3 * n + hop + 1; ++i) {
        random = 1664525 * random + 1013904223;
        const float sample = i == n ? std::numeric_limits<float>::quiet_NaN()
          : i < 2 * n ? .3f * (static_cast<double>(random) / 4294967296.0 - .5) : 0;
        const bool now = immediate.push(sample), later = deferred.push(sample);
        require(now == ((i + 1) % hop == 0), "immediate boundary is unchanged");
        require(later == (i > 0 && i % hop == 0), "deferred completion is exactly one sample later");
        require(deferred.frameCount() == i / hop, "deferred count includes only completed transforms");
        if (now) require(deferred.spectrum().empty(), "pending window is not published as an FFT");
        if (later) {
          const auto expected = immediate.spectrum(), actual = deferred.spectrum();
          require(actual.size() == n, "completed deferred spectrum has full size");
          for (std::size_t bin = 0; bin < n; ++bin) {
            require(std::bit_cast<std::uint32_t>(actual[bin].real()) == std::bit_cast<std::uint32_t>(expected[bin].real())
                    && std::bit_cast<std::uint32_t>(actual[bin].imag()) == std::bit_cast<std::uint32_t>(expected[bin].imag()),
                    "deferred FFT preserves window timestamp and every bin bit");
          }
        }
      }
      deferred.reset();
      for (std::size_t i = 0; i < hop; ++i) require(!deferred.push(.25f), "window waits for deferred execution");
      require(deferred.spectrum().empty(), "reset test has a pending transform");
      deferred.reset(); deferred.reset();
      require(!deferred.push(0) && deferred.frameCount() == 0, "reset discards pending FFT and its timeline");
      for (std::size_t i = 1; i <= hop; ++i) {
        if (deferred.push(0)) {
          for (const auto value : deferred.spectrum()) require(value == std::complex<float>{}, "reset clears staged window/history");
        }
      }
      require(deferred.frameCount() == 1, "deferred reset retains the preparation mode");
    }
  }
  auto oneSample = std::make_shared<SpectralPlan>(32, 1);
  SpectralAnalysis analysis; analysis.prepare(oneSample);
  require(analysis.push(.25f), "default one-sample hop stays supported");
  bool rejected = false;
  try { analysis.prepare(oneSample, true); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected && analysis.push(0), "deferred one-sample hop rejected before changing prepared state");
}

void compactSynthesis() {
  using namespace ardor::pog3;
  for (const std::size_t n : {32U, 1024U, 2048U, 4096U}) {
    const auto hop = n / 8;
    auto plan = std::make_shared<SpectralPlan>(n, hop);
    SpectralAnalysis analysis; analysis.prepare(plan);
    SpectralSynthesis reference, compact;
    reference.prepare(plan); compact.prepare(plan, true, hop);
    require(!compact.addFrame({}), "external-scratch synthesis rejects immutable/empty input safely");
    std::vector<std::complex<float>> scratch(n), invalid(n);
    std::uint32_t random = 0x434f4d50;
    // Different ring lengths, both staged-offset edges and many ring wraps.
    for (std::size_t i = 0; i < 19013 + 2 * n; ++i) {
      require(compact.pop() == reference.pop(), "compact staged WOLA is bit-identical to general WOLA");
      random = 1664525 * random + 1013904223;
      const float x = i < 19013 ? .3f * (static_cast<double>(random) / 4294967296.0 - .5) : 0;
      if (!analysis.push(x)) continue;
      std::copy(analysis.spectrum().begin(), analysis.spectrum().end(), scratch.begin());
      const auto offset = analysis.frameCount() % 2 ? 0 : hop;
      require(reference.addFrame(analysis.spectrum(), offset) && compact.addFrameInPlace(scratch, offset),
              "both staged synthesis paths accept complete spectra");
      require(!compact.addFrameInPlace(scratch, hop + 1), "compact synthesis enforces its prepared staging bound");
      invalid.back() = {std::numeric_limits<float>::quiet_NaN(), 0};
      require(!compact.addFrameInPlace(invalid), "nonfinite external scratch preserves pending output");
      std::fill(invalid.begin(), invalid.end(), std::complex<float>{std::numeric_limits<float>::max(), 0});
      require(!compact.addFrameInPlace(invalid), "inverse overflow preserves compact pending output");
      std::fill(invalid.begin(), invalid.end(), std::complex<float>{});
    }
    compact.reset(); compact.reset();
    for (std::size_t i = 0; i < 2 * n; ++i) require(compact.pop() == 0, "compact repeated reset clears wrapped OLA");
  }
}

using pog3_test::writeRender;

void renderSpectral(const std::filesystem::path& directory,
                    const std::vector<ardor::StereoSample>& input, bool isolated, bool warp) {
  using namespace ardor::pog3;
  constexpr std::array<const char*, 5> names{"down2", "down1", "fifth", "up1", "up2"};
  for (const bool focus : {false, true}) {
    PolyphonicPitchBank bank;
    bank.setFocus(focus);
    if (warp) require(bank.setWarp(0), "initial render Warp heel");
    bank.prepare();
    std::array<std::vector<ardor::StereoSample>, 5> voices;
    std::vector<ardor::StereoSample> mix(input.size());
    if (isolated) for (auto& voice : voices) voice.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
      if (warp && i % 480 == 0) {
        const float t = i / 48000.0f;
        const float position = t < 2 ? t / 2 : (4 - t) / 2;
        require(bank.setWarp(std::round(position * 127) / 127), "render Warp update");
      }
      const auto output = bank.process({input[i].left, input[i].right});
      mix[i] = {input[i].left * .25f, input[i].right * .25f}; // Immediate dry, same reference mix.
      for (std::size_t v = 1; v < kVoiceCount; ++v) {
        if (isolated) voices[v - 1][i] = {output[v].left, output[v].right};
        mix[i].left += output[v].left * .25f;
        mix[i].right += output[v].right * .25f;
      }
    }
    require(bank.healthy() && bank.deadlineMisses() == 0, "render bank remains healthy");
    const std::string suffix = focus ? "-focus-on.wav" : "-focus-off.wav";
    if (isolated) for (std::size_t v = 0; v < 5; ++v)
      writeRender(directory / (std::string("spectral-") + names[v] + "-196Hz" + suffix), voices[v]);
    else writeRender(directory / (std::string(warp ? "spectral-warp" : "spectral-chord-all-voices") + suffix), mix);
  }
}

void renderReference(const std::filesystem::path& directory) {
  using namespace ardor::pog3;
  std::filesystem::create_directories(directory);
  constexpr std::size_t length = 4 * 48000;
  std::vector<ardor::StereoSample> input(length), output(length);
  pog3_test::GranularReference reference;
  auto values = defaultValues();
  values[index(Parameter::DryLevel)] = 0;
  for (std::size_t i = 0; i < length; ++i) {
    const float x = .2 * std::sin(kTwoPi * 196.0 * i / 48000.0);
    input[i] = {x, -.73f * x};
  }
  writeRender(directory / "input-196Hz-stereo.wav", input);
  constexpr std::array<const char*, 5> names{"down2", "down1", "fifth", "up1", "up2"};
  for (std::size_t voice = 1; voice < kVoiceCount; ++voice) {
    reference.reset();
    for (std::size_t v = 1; v < kVoiceCount; ++v) values[v + 1] = v == voice ? 1 : 0;
    reference.setValues(values);
    for (std::size_t i = 0; i < length; ++i) output[i] = reference.process(input[i]);
    writeRender(directory / (std::string("reference-") + names[voice - 1] + "-196Hz.wav"), output);
  }
  renderSpectral(directory, input, true, false);
  // Chord notes overlap, with a second bass note entering halfway through.
  for (std::size_t i = 0; i < length; ++i) {
    const double t = static_cast<double>(i) / 48000;
    const double envelope = std::min(t / .005, 1.0) * std::min((4 - t) / .050, 1.0);
    float left = 0, right = 0;
    for (const double frequency : {82.4069, 130.8128, 164.8138, 196.0, 261.6256}) {
      left += .025 * (std::sin(kTwoPi * frequency * t) + .25 * std::sin(kTwoPi * 3 * frequency * t));
      right += .025 * (std::sin(kTwoPi * frequency * t + .3) + .25 * std::sin(kTwoPi * 3 * frequency * t));
    }
    if (t > 2) {
      const double onset = std::min((t - 2) / .005, 1.0);
      left += .04 * onset * std::sin(kTwoPi * 110 * t);
      right -= .04 * onset * std::sin(kTwoPi * 110 * t);
    }
    input[i] = {static_cast<float>(left * envelope), static_cast<float>(right * envelope)};
  }
  writeRender(directory / "input-chord-bass-onset.wav", input);
  values = defaultValues();
  for (std::size_t v = 0; v < kVoiceCount; ++v) values[v + 1] = .25f;
  reference.setValues(values);
  reference.reset();
  for (std::size_t i = 0; i < length; ++i) output[i] = reference.process(input[i]);
  writeRender(directory / "reference-chord-all-voices.wav", output);
  for (const bool focus : {false, true}) {
    reference.reset();
    for (std::size_t i = 0; i < length; ++i) {
      // 7-bit pedal updates at 100 Hz: heel -> toe -> heel over four seconds.
      if (i % 480 == 0) {
        const float t = i / 48000.0f;
        const float position = t < 2 ? t / 2 : (4 - t) / 2;
        reference.setWarp(std::round(position * 127) / 127, focus);
      }
      output[i] = reference.process(input[i]);
    }
    writeRender(directory / (focus ? "reference-warp-focus-on.wav" : "reference-warp-focus-off.wav"), output);
  }
  reference.setWarp(1, true);
  renderSpectral(directory, input, false, false);
  renderSpectral(directory, input, false, true);
  std::cout << "Reference artifacts only: granular chord/Warp artifacts are intentionally retained for comparison.\n";
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 1 && !(argc == 3 && std::string_view(argv[1]) == "--render"))
      throw std::runtime_error("usage: pedal-pog3-quality [--render directory]");
    preparedTransform();
    sharedPlanExecution();
    spectralIdentity();
    spectralLifecycle();
    deferredAnalysis();
    compactSynthesis();
    granularReference();
    if (argc == 3) renderReference(argv[2]);
    std::cout << "POG3 foundation/reference checks passed; spectral gates run in pedal-pog3-pitch-quality\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
