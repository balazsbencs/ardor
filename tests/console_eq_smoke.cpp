#include "equalizer/ConsoleEqProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace ardor;
namespace {
constexpr float kRate = 48000;
void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
ConsoleEqProcessor make(const nlohmann::json& params, float rate = kRate) {
  ConsoleEqProcessor processor;
  std::string error;
  require(processor.configure(params, rate, error), error);
  return processor;
}
nlohmann::json with(nlohmann::json params, const char* key, float value) {
  params[key] = value;
  return params;
}
float sine(float amplitude, float hz, int i, float rate = kRate) {
  return amplitude * static_cast<float>(std::sin(2 * std::numbers::pi * hz * i / rate));
}
std::vector<float> render(const nlohmann::json& params, float amplitude, float hz, int frames) {
  auto processor = make(params);
  std::vector<float> out(frames);
  for (int i = 0; i < frames; ++i) out[i] = processor.process({sine(amplitude, hz, i), 0}).left;
  return out;
}
double rmsDb(const std::vector<float>& y, std::size_t from) {
  double sum = 0;
  for (std::size_t i = from; i < y.size(); ++i) sum += static_cast<double>(y[i]) * y[i];
  return 10 * std::log10(sum / static_cast<double>(y.size() - from));
}
float response(const nlohmann::json& params, float hz, float rate = kRate) {
  auto processor = make(params, rate);
  double sum = 0;
  const int frames = static_cast<int>(rate);
  for (int i = 0; i < frames; ++i) {
    const auto y = processor.process({sine(0.001f, hz, i, rate), 0});
    require(std::isfinite(y.left) && y.right == 0, "finite output and no stereo crosstalk");
    if (i >= frames / 2) sum += static_cast<double>(y.left) * y.left;
  }
  return 20 * std::log10(std::sqrt(sum / (frames / 2)) / (0.001 / std::sqrt(2.0)));
}
float relative(const nlohmann::json& params, float hz) {
  return response(params, hz) - response(defaultConsoleEqParams(), hz);
}
// Hann-windowed magnitude of one frequency over the second half of a render.
double magnitude(const std::vector<float>& y, double hz) {
  const std::size_t start = y.size() / 2, n = y.size() - start;
  std::complex<double> sum{};
  for (std::size_t i = 0; i < n; ++i) {
    const double window = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / n);
    sum += window * y[start + i] * std::polar(1.0, -2 * std::numbers::pi * hz * i / kRate);
  }
  return std::abs(sum);
}
// Largest harmonic that folded back below 16 kHz, relative to the fundamental.
double worstAliasDbc(const std::vector<float>& y, double hz) {
  double worst = 1e-30;
  for (int k = 2; k < 200; ++k) {
    if (k * hz < kRate / 2) continue;
    double folded = std::fmod(k * hz, static_cast<double>(kRate));
    if (folded > kRate / 2) folded = kRate - folded;
    if (folded >= 20 && folded <= 16000) worst = std::max(worst, magnitude(y, folded));
  }
  return 20 * std::log10(worst / magnitude(y, hz));
}

void testResponse(const nlohmann::json& defaults) {
  for (float rate : {32000.f, 44100.f, 48000.f, 96000.f, 192000.f})
    require(std::abs(response(defaults, 1000, rate)) < 0.1, "neutral default response");
  for (const auto& [key, freq, gain] : std::array<std::tuple<const char*, float, float>, 3>{{
      {"low_db", 5, 16}, {"mid_db", 1600, 18}, {"high_db", 19000, 16}}}) {
    const float boost = relative(with(defaults, key, gain), freq);
    const float cut = relative(with(defaults, key, -gain), freq);
    require(boost > gain - 2 && boost < gain + 0.2f, "band reaches expected boost: " + std::string(key));
    require(std::abs(boost + cut) < 0.15f, "boost/cut curves are reciprocal");
  }
  constexpr float lowHz[] = {0, 35, 60, 110, 220};
  for (int band = 1; band <= 4; ++band) {
    const auto params = with(with(defaults, "low_freq", band), "low_db", 12);
    require(std::abs(relative(params, lowHz[band]) - 6) < 0.1, "all four low shelf corners");
  }
  constexpr float midHz[] = {0, 360, 700, 1600, 3200, 4800, 7200};
  for (int band = 1; band <= 6; ++band) {
    const auto params = with(with(defaults, "mid_freq", band), "mid_db", 12);
    require(std::abs(relative(params, midHz[band]) - 12) < 0.1, "all six mid frequency positions");
  }
  constexpr float passHz[] = {0, 50, 80, 160, 300};
  for (int band = 1; band <= 4; ++band) {
    const auto params = with(defaults, "high_pass", band);
    require(std::abs(relative(params, passHz[band]) + 3.0103f) < 0.15f, "Butterworth cutoff is -3 dB");
    const float a = relative(params, passHz[band] / 4), b = relative(params, passHz[band] / 8);
    require(std::abs(a - b - 18.0618f) < 0.15f, "HPF rolls off at 18 dB/octave");
  }
  auto disabled = with(with(defaults, "low_freq", 0), "mid_freq", 0);
  disabled["low_db"] = 16;
  disabled["mid_db"] = 18;
  require(std::abs(relative(disabled, 360)) < 0.01f, "Off disables the whole band");
}

void testSaturation(const nlohmann::json& defaults) {
  const auto clean = render(defaults, 0.25f, 220, 48000);
  const double cleanDb = rmsDb(clean, 4800);
  double previousThird = 20 * std::log10(magnitude(clean, 660) / magnitude(clean, 220));
  for (float amount : {0.25f, 0.5f, 0.75f, 1.0f}) {
    const auto driven = render(with(defaults, "saturation", amount), 0.25f, 220, 48000);
    const double change = rmsDb(driven, 4800) - cleanDb;
    const double third = 20 * std::log10(magnitude(driven, 660) / magnitude(driven, 220));
    std::printf("saturation %.2f: level %+.2f dB, third harmonic %.1f dBc\n", amount, change, third);
    require(std::abs(change) < 2, "saturation keeps the level of a -12 dBFS signal");
    require(third > previousThird, "more saturation adds more harmonics");
    previousThird = third;
  }
  require(previousThird > -30, "full saturation is clearly audible");
  for (float hz : {1531.f, 2731.f, 4019.f, 6007.f}) {
    const double alias = worstAliasDbc(render(with(defaults, "saturation", 1), 0.5f, hz, 96000), hz);
    std::printf("full saturation, %.0f Hz at -6 dBFS: worst alias %.1f dBc\n", hz, alias);
    require(alias < -70, "ADAA keeps folded harmonics below -70 dBc");
  }
}

void testMixPolarityAndDry(const nlohmann::json& defaults) {
  auto dry = make(with(with(with(defaults, "mix", 0), "saturation", 1), "high_db", 16));
  std::array<float, ConsoleEqProcessor::kLatencyFrames> history{};
  for (int i = 0; i < 1000; ++i) {
    const float x = std::sin(i * .12f);
    const auto y = dry.process({x, x * .5f});
    require(y.left == history[i % history.size()] && y.right == y.left * .5f, "dry mix is exact and latency aligned");
    history[i % history.size()] = x;
  }
  for (float mix : {1.0f, 0.5f}) {
    auto normal = make(with(defaults, "mix", mix));
    auto inverted = make(with(with(defaults, "mix", mix), "polarity", 1));
    double normalEnergy = 0, invertedEnergy = 0;
    for (int i = 0; i < 4800; ++i) {
      const float x = .5f * std::sin(i * .12f);
      const auto a = normal.process({x, -x}), b = inverted.process({x, -x});
      require(a.left == -b.left && a.right == -b.right, "polarity inverts the whole output");
      normalEnergy += static_cast<double>(a.left) * a.left;
      invertedEnergy += static_cast<double>(b.left) * b.left;
    }
    require(std::abs(10 * std::log10(invertedEnergy / normalEnergy)) < 0.01, "inverted polarity keeps the level at any Mix");
  }
}

// Residual against a processor that started with the destination setting.
double switchResidual(const nlohmann::json& from, const char* key, float to, std::size_t settleFrames) {
  auto switched = make(from), settled = make(with(from, key, to));
  const int switchAt = 24000 / 32 * 32;
  double peak = 0;
  for (int i = 0; i < 48000; ++i) {
    const float x = sine(0.3f, 82.4f, i) + sine(0.1f, 1100, i);
    if (i == switchAt) require(switched.setParameterTarget(key, to), "switch accepted");
    const float a = switched.process({x, x}).left, b = settled.process({x, x}).left;
    if (i > switchAt + static_cast<int>(settleFrames)) peak = std::max(peak, static_cast<double>(std::abs(a - b)));
  }
  return peak;
}

void testSwitchFades(const nlohmann::json& defaults) {
  auto shaped = with(with(with(defaults, "high_pass", 2), "low_db", 12), "low_freq", 3);
  shaped["mid_db"] = 6;
  const double mid = switchResidual(shaped, "mid_freq", 5, 480);
  std::printf("mid switch residual after 10 ms: %.2e\n", mid);
  require(mid < 1e-4, "a mid switch keeps the HPF and low shelf history");
  const double pass = switchResidual(shaped, "high_pass", 3, 4800);
  std::printf("HPF switch residual after 100 ms: %.2e\n", pass);
  require(pass < 1e-3, "an HPF switch settles after its long fade");
}

void testMonoBlocks(const nlohmann::json& defaults) {
  const auto params = with(with(with(defaults, "saturation", 0.6f), "mid_db", 9), "low_db", -4);
  auto mono = make(params), stereo = make(params);
  std::array<float, 64> inL{}, inR{}, outL{}, outR{};
  for (int block = 0; block < 40; ++block) {
    const bool split = block >= 20;
    for (std::size_t i = 0; i < inL.size(); ++i) {
      const int n = block * 64 + static_cast<int>(i);
      inL[i] = sine(0.4f, 196, n);
      inR[i] = split ? sine(0.2f, 330, n) : inL[i];
    }
    if (block == 10) {
      mono.setParameterTarget("mid_freq", 6);
      stereo.setParameterTarget("mid_freq", 6);
    }
    mono.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), inL.size(), split);
    for (std::size_t i = 0; i < inL.size(); ++i) {
      const auto y = stereo.process({inL[i], inR[i]});
      require(outL[i] == y.left && outR[i] == y.right, "mono block path matches the stereo path exactly");
    }
  }
}

void testAutomationAndReset(const nlohmann::json& defaults) {
  nlohmann::json loudest = defaults;
  for (const auto& c : kConsoleEqControls) loudest[c.key] = c.maximum;
  loudest["polarity"] = 0;
  float steadyPeak = 0;
  auto steady = make(loudest), automated = make(defaults);
  float automatedPeak = 0;
  for (int i = 0; i < 16000; ++i) {
    if (i % 47 == 0) {
      for (std::size_t j = 0; j < kConsoleEqControls.size(); ++j) {
        const auto& c = kConsoleEqControls[j];
        require(automated.setParameterTarget(c.key, (i / 47 + j) % 2 ? c.minimum : c.maximum), "automation supported");
      }
    }
    const StereoSample x{.1f * std::sin(i * .17f), .1f * std::cos(i * .07f)};
    const auto a = automated.process(x), s = steady.process(x);
    require(std::isfinite(a.left) && std::isfinite(a.right), "rapid switching remains finite");
    automatedPeak = std::max({automatedPeak, std::abs(a.left), std::abs(a.right)});
    if (i > 4800) steadyPeak = std::max({steadyPeak, std::abs(s.left), std::abs(s.right)});
  }
  std::printf("automation peak %.2f, loudest steady setting %.2f\n", automatedPeak, steadyPeak);
  require(automatedPeak < 2 * steadyPeak, "automation stays near the loudest steady setting");
  require(automated.setParameterTarget("low_db", 100), "valid controls clamp");
  require(!automated.setParameterTarget("bad", 0) && !automated.setParameterTarget("mix", NAN),
          "invalid live controls rejected");
  automated.reset();
  for (int i = 0; i < 2000; ++i) {
    const auto y = automated.process({0, 0});
    require(y.left == 0 && y.right == 0, "reset clears tails and silence has no added noise");
  }
  std::string error;
  ConsoleEqProcessor invalid;
  require(!invalid.configure(defaults, 0, error), "invalid sample rate rejected");
  auto wrongType = defaults;
  wrongType["low_db"] = "bad";
  require(!invalid.configure(wrongType, kRate, error), "invalid parameter types rejected");
  require(!invalid.configure(with(defaults, "low_db", std::numeric_limits<float>::infinity()), kRate, error),
          "non-finite config rejected");
}
}

int main() {
  const auto defaults = defaultConsoleEqParams();
  testResponse(defaults);
  testSaturation(defaults);
  testMixPolarityAndDry(defaults);
  testSwitchFades(defaults);
  testMonoBlocks(defaults);
  testAutomationAndReset(defaults);
  std::cout << "1073 EQ response, saturation, fades, mono blocks and automation passed\n";
}
