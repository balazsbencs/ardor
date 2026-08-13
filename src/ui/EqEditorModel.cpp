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

float eqBandwidthOctaves(float q)
{
  const float safeQ = std::max(q, kEqMinimumQ);
  return (2.0f / std::log(2.0f)) * std::asinh(1.0f / (2.0f * safeQ));
}

std::pair<float, float> eqShoulderFrequencies(float centerHz, float q)
{
  const float halfOctaves = eqBandwidthOctaves(q) / 2.0f;
  return {clampFrequency(centerHz * std::pow(2.0f, -halfOctaves)),
          clampFrequency(centerHz * std::pow(2.0f, halfOctaves))};
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
  case EqBandField::Slope:
    break;
  }
}

void adjustEqPassFilterField(EqPassFilterParams& filter, EqBandField field, int ticks)
{
  switch (field) {
  case EqBandField::Frequency:
    filter.frequencyHz = clampFrequency(
      filter.frequencyHz * std::pow(2.0f, static_cast<float>(ticks) / 24.0f));
    break;
  case EqBandField::Q:
    filter.q = std::clamp(filter.q * std::pow(2.0f, static_cast<float>(ticks) / 24.0f),
                          kEqMinimumQ, kEqMaximumQ);
    break;
  case EqBandField::Gain:
    break;
  case EqBandField::Slope: {
    const auto current = std::find(kEqPassFilterSlopesDbPerOctave.begin(),
                                   kEqPassFilterSlopesDbPerOctave.end(),
                                   normalizedEqPassFilterSlope(filter.slopeDbPerOctave));
    const auto index = static_cast<int>(std::distance(
      kEqPassFilterSlopesDbPerOctave.begin(), current));
    const auto next = std::clamp(index + ticks, 0,
      static_cast<int>(kEqPassFilterSlopesDbPerOctave.size()) - 1);
    filter.slopeDbPerOctave = kEqPassFilterSlopesDbPerOctave[static_cast<std::size_t>(next)];
    break;
  }
  }
}

EqCurveData makeEqCurveData(const ParametricEqParams& params, float sampleRate)
{
  EqCurveData data;
  const float safeSampleRate = std::max(sampleRate, 1000.0f);
  std::array<BiquadCoefficients, kParametricEqBandCount> coefficients{};
  const auto highPassCoefficients = makeHighPassCascade(
    safeSampleRate, params.highPass.frequencyHz, params.highPass.q,
    params.highPass.slopeDbPerOctave);
  const auto lowPassCoefficients = makeLowPassCascade(
    safeSampleRate, params.lowPass.frequencyHz, params.lowPass.q,
    params.lowPass.slopeDbPerOctave);
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
    if (params.highPass.enabled) {
      const float responseDb = cascadeMagnitudeDb(highPassCoefficients, frequency, safeSampleRate);
      data.stageDb[kEqHighPassStage][point] = clampGain(responseDb);
      combinedDb += responseDb;
    }
    for (std::size_t bandIndex = 0; bandIndex < params.bands.size(); ++bandIndex) {
      const auto& band = params.bands[bandIndex];
      if (!band.enabled) {
        data.stageDb[kEqFirstBandStage + bandIndex][point] = 0.0f;
        continue;
      }
      const float responseDb = biquadMagnitudeDb(coefficients[bandIndex], frequency, safeSampleRate);
      data.stageDb[kEqFirstBandStage + bandIndex][point] = clampGain(responseDb);
      combinedDb += responseDb;
    }
    if (params.lowPass.enabled) {
      const float responseDb = cascadeMagnitudeDb(lowPassCoefficients, frequency, safeSampleRate);
      data.stageDb[kEqLowPassStage][point] = clampGain(responseDb);
      combinedDb += responseDb;
    }
    data.combinedDb[point] = clampGain(combinedDb);
  }
  return data;
}

} // namespace ardor
