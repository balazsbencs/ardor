#pragma once

#include "equalizer/EqParameters.h"

#include <array>
#include <cstddef>
#include <utility>

namespace ardor {

inline constexpr std::size_t kEqCurvePointCount = 256;

enum class EqBandField {
  Frequency,
  Q,
  Gain,
};

struct EqCurveData {
  std::array<float, kEqCurvePointCount> frequencyHz{};
  std::array<std::array<float, kEqCurvePointCount>, kParametricEqBandCount> bandDb{};
  std::array<float, kEqCurvePointCount> combinedDb{};
};

int eqXFromFrequency(float frequencyHz, int width);
float eqFrequencyFromX(int x, int width);
int eqYFromGain(float gainDb, int height);
float eqGainFromY(int y, int height);
void adjustEqBandField(EqBandParams& band, EqBandField field, int ticks);
EqCurveData makeEqCurveData(const ParametricEqParams& params, float sampleRate);

// The band's -3 dB bandwidth in octaves, from the standard Q-to-bandwidth
// relationship. Used to place the shoulder grips; it is independent of gain,
// so it approximates rather than measures the biquad's actual half-power
// points.
float eqBandwidthOctaves(float q);
// The -3 dB shoulder frequencies either side of the centre, symmetric in log
// frequency about it.
std::pair<float, float> eqShoulderFrequencies(float centerHz, float q);

} // namespace ardor
