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
// RBJ cookbook high shelf. Passing a negated gainDb gives the exact magnitude
// inverse of a positive one, which is how a pre-emphasis and de-emphasis pair
// is built out of a single function.
BiquadCoefficients makeHighShelf(float sampleRate, float frequencyHz, float q, float gainDb);
PassFilterCascade makeHighPassCascade(float sampleRate, float frequencyHz, float q,
                                      int slopeDbPerOctave);
PassFilterCascade makeLowPassCascade(float sampleRate, float frequencyHz, float q,
                                     int slopeDbPerOctave);
float cascadeMagnitudeDb(const PassFilterCascade& cascade, float frequencyHz, float sampleRate);
float biquadMagnitudeDb(const BiquadCoefficients& coefficients, float frequencyHz, float sampleRate);

} // namespace ardor
