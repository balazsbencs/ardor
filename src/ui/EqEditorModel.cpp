#include "ui/EqEditorModel.h"

#include "equalizer/ParametricEqMath.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

float clampFrequency(float frequencyHz)
{
  return std::clamp(frequencyHz, kEqMinimumFrequencyHz, kEqMaximumFrequencyHz);
}

float clampGain(float gainDb)
{
  return std::clamp(gainDb, kEqMinimumGainDb, kEqMaximumGainDb);
}

float normalizedLogFrequency(float frequencyHz)
{
  const float low = std::log(kEqMinimumFrequencyHz);
  const float span = std::log(kEqMaximumFrequencyHz) - low;
  return (std::log(clampFrequency(frequencyHz)) - low) / span;
}

} // namespace

int eqXFromFrequency(float frequencyHz, int width)
{
  if (width <= 1) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::lround(normalizedLogFrequency(frequencyHz) * (width - 1))), 0, width - 1);
}

float eqFrequencyFromX(int x, int width)
{
  if (width <= 1) {
    return kEqMinimumFrequencyHz;
  }
  const float fraction = static_cast<float>(std::clamp(x, 0, width - 1)) / static_cast<float>(width - 1);
  return kEqMinimumFrequencyHz * std::exp(fraction * std::log(kEqMaximumFrequencyHz / kEqMinimumFrequencyHz));
}

int eqYFromGain(float gainDb, int height)
{
  if (height <= 1) {
    return 0;
  }
  const float fraction = (kEqMaximumGainDb - clampGain(gainDb)) / (kEqMaximumGainDb - kEqMinimumGainDb);
  return std::clamp(static_cast<int>(std::lround(fraction * (height - 1))), 0, height - 1);
}

float eqGainFromY(int y, int height)
{
  if (height <= 1) {
    return 0.0f;
  }
  const float fraction = static_cast<float>(std::clamp(y, 0, height - 1)) / static_cast<float>(height - 1);
  return kEqMaximumGainDb - fraction * (kEqMaximumGainDb - kEqMinimumGainDb);
}

void adjustEqBandField(EqBandParams& band, EqBandField field, int ticks)
{
  switch (field) {
  case EqBandField::Frequency:
    band.frequencyHz = clampFrequency(band.frequencyHz * std::pow(2.0f, static_cast<float>(ticks) / 24.0f));
    break;
  case EqBandField::Q:
    band.q = std::clamp(band.q * std::pow(2.0f, static_cast<float>(ticks) / 24.0f),
                        kEqMinimumQ, kEqMaximumQ);
    break;
  case EqBandField::Gain:
    band.gainDb = clampGain(band.gainDb + static_cast<float>(ticks) * 0.5f);
    break;
  }
}

EqCurveData makeEqCurveData(const ParametricEqParams& params, float sampleRate)
{
  EqCurveData data;
  const float safeSampleRate = std::max(sampleRate, 1000.0f);
  std::array<BiquadCoefficients, kParametricEqBandCount> coefficients{};
  for (std::size_t bandIndex = 0; bandIndex < params.bands.size(); ++bandIndex) {
    const auto& band = params.bands[bandIndex];
    if (band.enabled) {
      coefficients[bandIndex] = makePeakingEq(safeSampleRate, band.frequencyHz, band.q, band.gainDb);
    }
  }
  for (std::size_t point = 0; point < kEqCurvePointCount; ++point) {
    const float fraction = static_cast<float>(point) / static_cast<float>(kEqCurvePointCount - 1);
    float frequency = kEqMinimumFrequencyHz
      * std::exp(fraction * std::log(kEqMaximumFrequencyHz / kEqMinimumFrequencyHz));
    if (point == 0) {
      frequency = kEqMinimumFrequencyHz;
    } else if (point == kEqCurvePointCount - 1) {
      frequency = kEqMaximumFrequencyHz;
    }
    data.frequencyHz[point] = frequency;
    float combinedDb = 0.0f;
    for (std::size_t bandIndex = 0; bandIndex < params.bands.size(); ++bandIndex) {
      const auto& band = params.bands[bandIndex];
      if (!band.enabled) {
        data.bandDb[bandIndex][point] = 0.0f;
        continue;
      }
      const float responseDb = biquadMagnitudeDb(coefficients[bandIndex], frequency, safeSampleRate);
      data.bandDb[bandIndex][point] = clampGain(responseDb);
      combinedDb += responseDb;
    }
    data.combinedDb[point] = clampGain(combinedDb);
  }
  return data;
}

} // namespace ardor
