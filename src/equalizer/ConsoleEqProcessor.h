#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "equalizer/ParametricEqMath.h"
#include <array>
#include <atomic>
#include <memory>
#include <string_view>

namespace ardor {

struct ConsoleEqControl {
  std::string_view key, label;
  float minimum, maximum, step, defaultValue;
  // Switches store integer positions, including Off at position zero.
  std::string_view choices;
};
inline constexpr std::array<ConsoleEqControl, 10> kConsoleEqControls{{
  {"low_db", "Low gain", -16, 16, 0.5f, 0, ""},
  {"low_freq", "Low frequency", 0, 4, 1, 2, "Off|35 Hz|60 Hz|110 Hz|220 Hz"},
  {"mid_db", "Mid gain", -18, 18, 0.5f, 0, ""},
  {"mid_freq", "Mid frequency", 0, 6, 1, 3, "Off|360 Hz|700 Hz|1.6 kHz|3.2 kHz|4.8 kHz|7.2 kHz"},
  {"high_db", "High gain · 12 kHz", -16, 16, 0.5f, 0, ""},
  {"high_pass", "High-pass · 18 dB/oct", 0, 4, 1, 0, "Off|50 Hz|80 Hz|160 Hz|300 Hz"},
  {"saturation", "Saturation", 0, 1, 0.01f, 0, ""},
  {"output_db", "Output trim", -24, 24, 0.5f, 0, ""},
  {"polarity", "Polarity", 0, 1, 1, 0, "Normal|Inverted"},
  {"mix", "Mix", 0, 1, 0.01f, 1, ""},
}};
nlohmann::json defaultConsoleEqParams();

// Original MIT implementation. Classic 1073 control points with cookbook
// shelves/bell, a third-order Butterworth HPF, and optional 2x tanh saturation.
// This is a musical approximation, not an emulation of a Neve circuit.
class ConsoleEqProcessor {
public:
  ConsoleEqProcessor() = default;
  ConsoleEqProcessor(ConsoleEqProcessor&&) noexcept = default;
  ConsoleEqProcessor& operator=(ConsoleEqProcessor&&) noexcept = default;
  ConsoleEqProcessor(const ConsoleEqProcessor&) = delete;
  ConsoleEqProcessor& operator=(const ConsoleEqProcessor&) = delete;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(std::string_view key, float value);
  StereoSample process(StereoSample input);
  void reset();
  static constexpr std::size_t kLatencyFrames = 15;
  std::size_t latencyFrames() const noexcept { return kLatencyFrames; }

private:
  struct Biquad {
    BiquadCoefficients c{};
    double z1 = 0, z2 = 0;
    float process(float x);
  };
  struct Bank {
    std::array<std::array<Biquad, 5>, 2> filters{};
    std::array<int, 3> switches{};
    StereoSample process(StereoSample input);
  };
  struct Channel {
    pedal::HalfbandInterpolator2x up;
    pedal::HalfbandDecimator2x down;
    std::array<float, kLatencyFrames> dry{};
    std::size_t index = 0;
  };
  struct Targets { std::array<std::atomic<float>, kConsoleEqControls.size()> values{}; };
  void updateControls();
  void updateBank(Bank& bank);
  float sampleRate_ = 48000;
  float controlStep_ = 1, sampleStep_ = 1;
  std::shared_ptr<Targets> targets_;
  std::array<float, kConsoleEqControls.size()> current_{};
  std::array<Bank, 2> banks_{};
  std::array<Channel, 2> channels_{};
  int active_ = 0;
  std::size_t controlCountdown_ = 0, fadeRemaining_ = 0, fadeFrames_ = 480;
  float saturation_ = 0, output_ = 1, outputTarget_ = 1, polarity_ = 1, mix_ = 1;
};
} // namespace ardor
