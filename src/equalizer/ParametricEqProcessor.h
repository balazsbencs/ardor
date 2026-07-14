#pragma once

#include "equalizer/EqParameters.h"
#include "equalizer/ParametricEqMath.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <string>

namespace ardor {

class ParametricEqProcessor {
public:
  bool configure(const ParametricEqParams& params, float sampleRate, std::string& error);
  bool setBandTarget(std::size_t index, const EqBandParams& params);
  void process(float& left, float& right);
  void processBlock(const float* inputLeft, const float* inputRight,
                    float* outputLeft, float* outputRight, std::size_t frames);
  void reset();

private:
  struct AtomicBand {
    std::atomic<bool> enabled{true};
    std::atomic<float> frequencyHz{1000.0f};
    std::atomic<float> q{1.0f};
    std::atomic<float> gainDb{0.0f};
  };

  struct FilterState {
    float z1 = 0.0f;
    float z2 = 0.0f;

    float process(float input, const BiquadCoefficients& coefficients);
  };

  void updateCoefficients(std::size_t frames);
  void processPrepared(float& left, float& right);

  std::array<AtomicBand, kParametricEqBandCount> targets_{};
  std::array<EqBandParams, kParametricEqBandCount> current_{};
  std::array<BiquadCoefficients, kParametricEqBandCount> coefficients_{};
  std::array<std::array<FilterState, 2>, kParametricEqBandCount> states_{};
  float sampleRate_ = 48000.0f;
  std::size_t scalarSamplesUntilUpdate_ = 0;
  bool configured_ = false;
};

} // namespace ardor
