#pragma once

#include "cheese/CheeseCircuit.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace ardor {

// Block-facing wrapper for the 8x oversampled circuit model.
//
// Eight, and four is nowhere near enough. This circuit clips asymmetrically, so
// it makes even harmonics as well as odd, and measuring the model on its own at
// 192 kHz shows them still at -30 dBc twelve harmonics up. Solved at that rate
// they fold before any filter sees them. Measured on a 5 kHz tone, where energy
// off a multiple of 5 kHz can only be aliasing, four times leaves the audible
// band at -28 to -40 dBc and eight times at -46 to -78 dBc.
//
// The one bin eight times does not fix is 23 kHz, which stays at -23 dBc. That
// is the fifth harmonic folding through the last decimator's transition band
// rather than anything the ratio can reach, and the RAT measures the same
// there. Nobody hears it.
//
// Six halfband stages, three up and three down, each 31 taps and linear phase.
//
// The circuit is mono, as the pedal is, and the result is returned on both
// channels.
class CheeseProcessor {
public:
  CheeseProcessor() = default;
  CheeseProcessor(CheeseProcessor&&) noexcept = default;
  CheeseProcessor& operator=(CheeseProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);
  // Group delay of the six halfband stages, referred to the host rate: 15
  // samples each at 96, 192, 384, 384, 192 and 96 kHz.
  std::size_t latencyFrames() const noexcept { return 26; }

private:
  CheeseProcessor(const CheeseProcessor&) = delete;
  CheeseProcessor& operator=(const CheeseProcessor&) = delete;

  CheeseCircuit circuit_;
  pedal::HalfbandInterpolator2x up2x_;
  pedal::HalfbandInterpolator2x up4x_;
  pedal::HalfbandInterpolator2x up8x_;
  pedal::HalfbandDecimator2x down4x_;
  pedal::HalfbandDecimator2x down2x_;
  pedal::HalfbandDecimator2x down1x_;

  float sampleRate_ = 48000.0f;
  float smoothing_ = 0.0f;

  float fuzzTarget_ = 0.7f;
  float toneTarget_ = 0.5f;
  float volumeTarget_ = 0.7f;
  float fuzz_ = 0.7f;
  float tone_ = 0.5f;
  float volume_ = 0.7f;

  // A knob turn rebuilds the state space, which costs a matrix factorisation
  // and a Newton solve for the operating point. That is far too much to do per
  // sample, so the smoothed values are only pushed into the circuit when one of
  // them has moved enough to be worth it. The step is below what a listener can
  // hear on a sweep and well above where the rebuild cost matters.
  static constexpr float kRebuildStep = 1.0f / 128.0f;
  float appliedFuzz_ = -1.0f;
  float appliedTone_ = -1.0f;
};

} // namespace ardor
