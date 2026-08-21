#include "tape/TapeHysteresis.h"

#include <algorithm>
#include <cmath>

namespace ardor {

float langevin(float x)
{
  if (std::fabs(x) < kLangevinTaylorLimit) {
    return x / 3.0f - (x * x * x) / 45.0f;
  }
  return 1.0f / std::tanh(x) - 1.0f / x;
}

float langevinPrime(float x)
{
  if (std::fabs(x) < kLangevinTaylorLimit) {
    return 1.0f / 3.0f - (x * x) / 15.0f;
  }
  // For large |x| sinh overflows to infinity and the second term becomes zero,
  // which is the correct limit — L'(x) tends to 1/x^2 — so no guard is needed.
  const float sinhX = std::sinh(x);
  return 1.0f / (x * x) - 1.0f / (sinhX * sinhX);
}

void TapeHysteresis::configure(const Parameters& parameters, float oversampledRate)
{
  setParameters(parameters);
  period_ = 1.0f / oversampledRate;
  // The envelope only has to hold across a low note's period, so 50 ms.
  envelopeRelease_ = std::exp(-1.0f / (0.05f * oversampledRate));
  reset();
}

void TapeHysteresis::setParameters(const Parameters& parameters) noexcept
{
  parameters_ = parameters;
}

void TapeHysteresis::reset()
{
  magnetisation_ = 0.0f;
  previousField_ = 0.0f;
  direction_ = 1.0f;
  extremeField_ = 0.0f;
  fieldEnvelope_ = 0.0f;
}

// Decides which branch of the loop the material is on, ignoring retraces too
// small to be real. See kDirectionDeadband for why a raw sign(dH/dt) is not
// good enough here.
float TapeHysteresis::trackDirection(float field)
{
  fieldEnvelope_ = std::max(std::fabs(field), fieldEnvelope_ * envelopeRelease_);
  const float deadband = kDirectionDeadband * fieldEnvelope_;

  if (direction_ > 0.0f) {
    if (field > extremeField_) {
      extremeField_ = field;
    } else if (extremeField_ - field > deadband) {
      direction_ = -1.0f;
      extremeField_ = field;
    }
  } else {
    if (field < extremeField_) {
      extremeField_ = field;
    } else if (field - extremeField_ > deadband) {
      direction_ = 1.0f;
      extremeField_ = field;
    }
  }
  return direction_;
}

float TapeHysteresis::derivative(float magnetisation, float field, float fieldRate,
                                 float direction) const
{
  const float effectiveField = field + parameters_.interdomainCoupling * magnetisation;
  const float normalised = effectiveField / parameters_.anhystereticShape;
  const float anhysteretic = parameters_.saturationMagnetisation * langevin(normalised);
  const float anhystereticSlope =
    (parameters_.saturationMagnetisation / parameters_.anhystereticShape) * langevinPrime(normalised);

  const float difference = anhysteretic - magnetisation;

  // delta_M is not decoration. Without it the solver takes unphysical branches
  // whenever the field reverses inside a minor loop, and the state grows
  // without bound on a signal that changes direction thousands of times a
  // second — which is what a guitar signal is.
  const float branch = (direction * difference) > 0.0f ? 1.0f : 0.0f;

  const float reversible = parameters_.reversibility;
  float denominator = (1.0f - reversible) * direction * parameters_.coercivity
                    - parameters_.interdomainCoupling * difference;

  // The denominator passes through zero at the tips of the loop. Floor it away
  // from zero, keeping its sign, so the ratio stays finite there.
  const float floorMagnitude = 1.0e-6f * parameters_.coercivity;
  if (std::fabs(denominator) < floorMagnitude) {
    denominator = denominator < 0.0f ? -floorMagnitude : floorMagnitude;
  }

  const float irreversible = ((1.0f - reversible) * branch * difference) / denominator;

  // The reversible term wants dM_an/dH, and dH_e/dH is 1 + alpha*dM/dH, which
  // would make this implicit. alpha is 1.6e-3, so using dM_an/dH_e directly
  // costs under 0.2% and keeps the step explicit. Documented, not overlooked.
  return (irreversible + reversible * anhystereticSlope) * fieldRate;
}

float TapeHysteresis::process(float field)
{
  const float fieldRate = (field - previousField_) / period_;
  const float direction = trackDirection(field);
  const float start = previousField_;
  const float middle = 0.5f * (start + field);
  const float halfStep = 0.5f * period_;

  const float k1 = derivative(magnetisation_, start, fieldRate, direction);
  const float k2 = derivative(magnetisation_ + halfStep * k1, middle, fieldRate, direction);
  const float k3 = derivative(magnetisation_ + halfStep * k2, middle, fieldRate, direction);
  const float k4 = derivative(magnetisation_ + period_ * k3, field, fieldRate, direction);

  magnetisation_ += (period_ / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);

  // The documented failure mode of a live Jiles-Atherton solver is divergence
  // at high drive. Magnetisation beyond saturation is not physical, so clamp
  // it rather than let a diverging step reach the output.
  if (!std::isfinite(magnetisation_)) magnetisation_ = 0.0f;
  const float ceiling = 2.0f * parameters_.saturationMagnetisation;
  magnetisation_ = std::clamp(magnetisation_, -ceiling, ceiling);

  previousField_ = field;
  return magnetisation_ / parameters_.saturationMagnetisation;
}

} // namespace ardor
