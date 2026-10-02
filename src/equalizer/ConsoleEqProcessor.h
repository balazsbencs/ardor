#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "equalizer/ConsoleSaturation.h"
#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqMath.h"
#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <string_view>

namespace ardor {

inline constexpr std::array<std::string_view, 5> kConsoleEqLowLabels{
  "Off", "35 Hz", "60 Hz", "110 Hz", "220 Hz"};
inline constexpr std::array<std::string_view, 7> kConsoleEqMidLabels{
  "Off", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz"};
inline constexpr std::array<std::string_view, 5> kConsoleEqHighPassLabels{
  "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz"};
inline constexpr std::array<std::string_view, 2> kConsoleEqPolarityLabels{"Normal", "Inverted"};
inline constexpr std::array<std::string_view, 2> kConsoleEqCharacterLabels{"Clean", "Console"};

struct ConsoleEqControl {
  std::string_view key, label;
  float minimum, maximum, step, defaultValue;
  // Switches store integer positions, including Off at position zero.
  std::span<const std::string_view> choices;
};
inline constexpr std::array<ConsoleEqControl, 11> kConsoleEqControls{{
  {"low_db", "Low gain", -16, 16, 0.5f, 0, {}},
  {"low_freq", "Low frequency", 0, 4, 1, 2, kConsoleEqLowLabels},
  {"mid_db", "Mid gain", -18, 18, 0.5f, 0, {}},
  {"mid_freq", "Mid frequency", 0, 6, 1, 3, kConsoleEqMidLabels},
  {"high_db", "High gain · 12 kHz", -16, 16, 0.5f, 0, {}},
  {"high_pass", "High-pass · 18 dB/oct", 0, 4, 1, 0, kConsoleEqHighPassLabels},
  {"saturation", "Saturation", 0, 1, 0.01f, 0, {}},
  {"character", "Character", 0, 1, 1, 1, kConsoleEqCharacterLabels},
  {"output_db", "Output trim", -24, 24, 0.5f, 0, {}},
  {"polarity", "Polarity", 0, 1, 1, 0, kConsoleEqPolarityLabels},
  {"mix", "Mix", 0, 1, 0.01f, 1, {}},
}};
nlohmann::json defaultConsoleEqParams();

// Original MIT implementation. Classic 1073 control points with cookbook
// shelves/bell, a third-order Butterworth HPF, and optional 2x saturation (see
// ConsoleSaturation). Character Console widens the mid at small gains, gives
// the low shelf a slight overshoot and adds the saturation character; with zero
// gains and no saturation both characters are identical and flat. This is a
// musical approximation, not an emulation of a Neve circuit.
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
  // With stereo false both inputs must carry the same signal: only the left
  // channel is computed and copied to both outputs.
  void processBlock(const float* inputLeft, const float* inputRight, float* outputLeft,
                    float* outputRight, std::size_t frames, bool stereo);
  void reset();
  static constexpr std::size_t kLatencyFrames = 15;
  std::size_t latencyFrames() const noexcept { return kLatencyFrames; }

private:
  static constexpr std::size_t kSections = 5;
  struct Biquad {
    BiquadCoefficients c{};
    double z1 = 0, z2 = 0;
    float process(float x);
  };
  struct Bank {
    std::array<std::array<Biquad, kSections>, 2> filters{};
    std::array<int, 4> switches{};
    float process(std::size_t channel, float input);
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
  void startFade(const std::array<int, 4>& switches);
  void beginFrame();
  float processChannel(std::size_t channel, float input);
  void endFrame();
  void copyLeftToRight();

  float sampleRate_ = 48000;
  float controlStep_ = 1, sampleStep_ = 1;
  std::shared_ptr<Targets> targets_;
  std::array<float, kConsoleEqControls.size()> current_{};
  std::array<Bank, 2> banks_{};
  std::array<Channel, 2> channels_{};
  ConsoleSaturation saturation_;
  std::array<ConsoleSaturation::State, 2> saturationStates_{};
  int active_ = 0;
  std::size_t controlCountdown_ = 0, fadeRemaining_ = 0, fadeLength_ = 1;
  std::size_t shortFadeFrames_ = 480, longFadeFrames_ = 1920;
  float blend_ = 0;
  float console_ = 1; // smoothed Character position for the saturation stage
  bool rightMirrorsLeft_ = false;
  float output_ = 1, outputTarget_ = 1, polarity_ = 1, mix_ = 1;
};
} // namespace ardor
