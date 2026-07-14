#include "equalizer/ParametricEqMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ardor {

BiquadCoefficients makePeakingEq(float sampleRate, float frequencyHz, float q, float gainDb)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    return {};
  }

  const float safeFrequency = std::clamp(
    std::isfinite(frequencyHz) ? frequencyHz : 1000.0f, 0.01f, sampleRate * 0.499f);
  const float safeQ = std::max(std::isfinite(q) ? q : 1.0f, 0.0001f);
  const float safeGainDb = std::isfinite(gainDb) ? gainDb : 0.0f;
  const double amplitude = std::pow(10.0, static_cast<double>(safeGainDb) / 40.0);
  const double omega = 2.0 * std::numbers::pi * safeFrequency / sampleRate;
  const double alpha = std::sin(omega) / (2.0 * safeQ);
  const double cosine = std::cos(omega);
  const double a0 = 1.0 + alpha / amplitude;

  return {
    static_cast<float>((1.0 + alpha * amplitude) / a0),
    static_cast<float>((-2.0 * cosine) / a0),
    static_cast<float>((1.0 - alpha * amplitude) / a0),
    static_cast<float>((-2.0 * cosine) / a0),
    static_cast<float>((1.0 - alpha / amplitude) / a0),
  };
}

float biquadMagnitudeDb(const BiquadCoefficients& coefficients, float frequencyHz, float sampleRate)
{
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f || !std::isfinite(frequencyHz)) {
    return 0.0f;
  }

  const double omega = 2.0 * std::numbers::pi * std::clamp(frequencyHz, 0.0f, sampleRate * 0.5f) / sampleRate;
  const double cosine = std::cos(omega);
  const double sine = std::sin(omega);
  const double cosine2 = std::cos(2.0 * omega);
  const double sine2 = std::sin(2.0 * omega);
  const double numerator = std::hypot(coefficients.b0 + coefficients.b1 * cosine + coefficients.b2 * cosine2,
                                      -coefficients.b1 * sine - coefficients.b2 * sine2);
  const double denominator = std::hypot(1.0 + coefficients.a1 * cosine + coefficients.a2 * cosine2,
                                        -coefficients.a1 * sine - coefficients.a2 * sine2);
  return static_cast<float>(20.0 * std::log10(std::max(numerator / std::max(denominator, 1.0e-12), 1.0e-12)));
}

} // namespace ardor
