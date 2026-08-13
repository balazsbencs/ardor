#pragma once

#include <array>
#include <cstddef>

namespace ardor {

struct BiquadCoefficients {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
};

struct PassFilterCascade {
  std::array<BiquadCoefficients, 2> sections{};
  std::size_t sectionCount = 0;
};

BiquadCoefficients makePeakingEq(float sampleRate, float frequencyHz, float q, float gainDb);
BiquadCoefficients makeHighPass(float sampleRate, float frequencyHz, float q);
BiquadCoefficients makeLowPass(float sampleRate, float frequencyHz, float q);
PassFilterCascade makeHighPassCascade(float sampleRate, float frequencyHz, float q,
                                      int slopeDbPerOctave);
PassFilterCascade makeLowPassCascade(float sampleRate, float frequencyHz, float q,
                                     int slopeDbPerOctave);
float cascadeMagnitudeDb(const PassFilterCascade& cascade, float frequencyHz, float sampleRate);
float biquadMagnitudeDb(const BiquadCoefficients& coefficients, float frequencyHz, float sampleRate);

} // namespace ardor
