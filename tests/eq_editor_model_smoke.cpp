#include "ui/EqEditorModel.h"

#include <cmath>
#include <iostream>

namespace {

int require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

bool nearlyEqual(float left, float right, float tolerance = 0.01f)
{
  return std::abs(left - right) <= tolerance;
}

} // namespace

int main()
{
  constexpr int width = 640;
  constexpr int height = 240;

  if (require(ardor::eqXFromFrequency(ardor::kEqMinimumFrequencyHz, width) == 0,
              "minimum frequency should map to the left edge")) return 1;
  if (require(ardor::eqXFromFrequency(ardor::kEqMaximumFrequencyHz, width) == width - 1,
              "maximum frequency should map to the right edge")) return 1;
  if (require(nearlyEqual(ardor::eqFrequencyFromX(ardor::eqXFromFrequency(1000.0f, width), width),
                          1000.0f, 5.0f),
              "frequency x mapping should round-trip on a logarithmic axis")) return 1;
  if (require(ardor::eqYFromGain(ardor::kEqMaximumGainDb, height) == 0,
              "maximum gain should map to the top edge")) return 1;
  if (require(ardor::eqYFromGain(ardor::kEqMinimumGainDb, height) == height - 1,
              "minimum gain should map to the bottom edge")) return 1;
  if (require(nearlyEqual(ardor::eqGainFromY(ardor::eqYFromGain(-6.0f, height), height), -6.0f, 0.2f),
              "gain y mapping should round-trip")) return 1;

  auto band = ardor::defaultParametricEqBand(2);
  ardor::adjustEqBandField(band, ardor::EqBandField::Gain, 2);
  if (require(nearlyEqual(band.gainDb, 1.0f), "gain adjustments should use 0.5 dB ticks")) return 1;
  ardor::adjustEqBandField(band, ardor::EqBandField::Frequency, 24);
  if (require(nearlyEqual(band.frequencyHz, 1600.0f, 0.1f), "frequency adjustments should double over 24 ticks")) return 1;
  ardor::adjustEqBandField(band, ardor::EqBandField::Q, -24);
  if (require(nearlyEqual(band.q, 0.5f, 0.01f), "Q adjustments should halve over 24 ticks")) return 1;

  auto params = ardor::defaultParametricEqParams();
  params.bands[0].frequencyHz = 1000.0f;
  params.bands[0].gainDb = 6.0f;
  params.bands[1].enabled = false;
  const auto curve = ardor::makeEqCurveData(params, 48000.0f);
  if (require(curve.frequencyHz.front() == ardor::kEqMinimumFrequencyHz,
              "curve should begin at the EQ minimum frequency")) return 1;
  if (require(curve.frequencyHz.back() == ardor::kEqMaximumFrequencyHz,
              "curve should end at the EQ maximum frequency")) return 1;
  const auto center = ardor::kEqCurvePointCount / 2;
  if (require(curve.combinedDb[center] > curve.bandDb[1][center] - 18.0f,
              "combined curve should be present for enabled bands")) return 1;
  if (require(nearlyEqual(curve.bandDb[1][center], 0.0f), "disabled bands should have a neutral individual response")) return 1;

  return 0;
}
