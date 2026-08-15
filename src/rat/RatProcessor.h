#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "rat/RatCircuit.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace ardor {

// Block-facing wrapper for the 8x oversampled circuit model.
//
// The oversampling is not optional, and four times is not enough. A hard-driven
// diode pair puts real energy above 100 kHz; solved at 192 kHz that energy folds
// straight back into the audio band with no filter in front of it. Measured on
// a 5 kHz tone at full distortion and the brightest filter setting, four times
// leaves the worst in-band alias at -40 dBc and eight times at -66 dBc, for
// exactly double the cost. The op-amp is the cheap half of this: it is slew
// limited to 0.3 V/us and cannot make a fast edge. The diodes can.
//
// Six halfband stages, three up and three down, each 31 taps and linear phase.
//
// The circuit is mono, as the pedal is, and the result is returned on both
// channels.
class RatProcessor {
public:
  RatProcessor() = default;
  RatProcessor(RatProcessor&&) noexcept = default;
  RatProcessor& operator=(RatProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);
  // Group delay of the six halfband stages, referred to the host rate: 15
  // samples each at 96, 192, 384, 384, 192 and 96 kHz.
  std::size_t latencyFrames() const noexcept { return 26; }

private:
  RatProcessor(const RatProcessor&) = delete;
  RatProcessor& operator=(const RatProcessor&) = delete;

  RatCircuit circuit_;
  pedal::HalfbandInterpolator2x up2x_;
  pedal::HalfbandInterpolator2x up4x_;
  pedal::HalfbandInterpolator2x up8x_;
  pedal::HalfbandDecimator2x down4x_;
  pedal::HalfbandDecimator2x down2x_;
  pedal::HalfbandDecimator2x down1x_;

  float sampleRate_ = 48000.0f;
  float smoothing_ = 0.0f;

  float distortionTarget_ = 0.5f;
  float filterTarget_ = 0.5f;
  float volumeTarget_ = 0.7f;
  float distortion_ = 0.5f;
  float filter_ = 0.5f;
  float volume_ = 0.7f;
};

} // namespace ardor
