#pragma once

#include "equalizer/EqParameters.h"

#include <array>
#include <cstddef>

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

} // namespace ardor
