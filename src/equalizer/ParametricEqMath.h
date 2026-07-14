#pragma once

namespace ardor {

struct BiquadCoefficients {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
};

BiquadCoefficients makePeakingEq(float sampleRate, float frequencyHz, float q, float gainDb);
float biquadMagnitudeDb(const BiquadCoefficients& coefficients, float frequencyHz, float sampleRate);

} // namespace ardor
