#include "tape/TapeHysteresis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

constexpr float kOversampledRate = 384000.0f;
constexpr float kTwoPi = 6.28318530718f;

// Drives the core with a triangular field sweep and records the magnetisation
// at every step, which traces the B-H loop directly.
std::vector<float> sweepLoop(float peakField, std::size_t stepsPerRamp)
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  std::vector<float> trace;
  trace.reserve(stepsPerRamp * 4);
  const auto ramp = [&](float from, float to) {
    for (std::size_t n = 0; n < stepsPerRamp; ++n) {
      const float t = static_cast<float>(n) / static_cast<float>(stepsPerRamp - 1);
      trace.push_back(core.process(from + (to - from) * t));
    }
  };
  // Two full cycles: the first magnetises from zero, the second is the loop.
  ramp(0.0f, peakField);
  ramp(peakField, -peakField);
  ramp(-peakField, peakField);
  ramp(peakField, -peakField);
  return trace;
}

void testLoopOpens()
{
  constexpr std::size_t kSteps = 2000;
  const auto trace = sweepLoop(4.0e4f, kSteps);
  // The third ramp climbs from -peak to +peak and the fourth falls back. At the
  // same field the two branches must differ: that difference IS hysteresis.
  // The midpoint of each ramp is field zero on both.
  const float ascending = trace[2 * kSteps + kSteps / 2];
  const float descending = trace[3 * kSteps + kSteps / 2];
  require(std::fabs(ascending - descending) > 0.02f,
          "the hysteresis loop must open: the two branches differ at the same field");
  require(ascending < descending,
          "the ascending branch must sit below the descending one");
}

void testLoopCloses()
{
  constexpr std::size_t kSteps = 2000;
  const auto trace = sweepLoop(4.0e4f, kSteps);
  // The second and fourth ramps are the same gesture one cycle apart. A solver
  // that drifts ends them in different places.
  const float firstCycleEnd = trace[2 * kSteps - 1];
  const float secondCycleEnd = trace[4 * kSteps - 1];
  require(std::fabs(firstCycleEnd - secondCycleEnd) < 0.01f,
          "the loop must close: the solver must not drift across cycles");
}

// Runs a sine at the oversampled rate and returns the magnitude of one
// harmonic, by direct correlation rather than an FFT.
double harmonicMagnitude(float fieldAmplitude, int harmonic)
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  constexpr float kFundamental = 1000.0f;
  const std::size_t settle = static_cast<std::size_t>(kOversampledRate / kFundamental) * 8;
  const std::size_t measure = static_cast<std::size_t>(kOversampledRate / kFundamental) * 64;

  for (std::size_t n = 0; n < settle; ++n) {
    const float t = static_cast<float>(n) / kOversampledRate;
    core.process(fieldAmplitude * std::sin(kTwoPi * kFundamental * t));
  }

  double real = 0.0;
  double imaginary = 0.0;
  for (std::size_t n = 0; n < measure; ++n) {
    const float t = static_cast<float>(settle + n) / kOversampledRate;
    const float out = core.process(fieldAmplitude * std::sin(kTwoPi * kFundamental * t));
    const double phase = kTwoPi * kFundamental * harmonic * t;
    real += out * std::cos(phase);
    imaginary += out * std::sin(phase);
  }
  return 2.0 * std::sqrt(real * real + imaginary * imaginary) / static_cast<double>(measure);
}

void testOddHarmonicsDominateAndGrow()
{
  const double third = harmonicMagnitude(3.0e4f, 3);
  const double second = harmonicMagnitude(3.0e4f, 2);
  std::printf("  third/second harmonic ratio: %.1f\n", third / second);
  require(third > second * 4.0,
          "tape is odd-symmetric: the third harmonic must dominate the second");

  const double quiet = harmonicMagnitude(1.0e4f, 3);
  const double loud = harmonicMagnitude(3.0e4f, 3);
  const double louder = harmonicMagnitude(6.0e4f, 3);
  require(quiet < loud && loud < louder,
          "the third harmonic must grow monotonically with drive");
}

void testBoundedUnderAbuse()
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  // A full-scale square is the worst case for an RK4 Jiles-Atherton solver:
  // dH/dt is enormous and reverses sign thousands of times a second. This is
  // the documented way these solvers diverge.
  for (std::size_t n = 0; n < 400000; ++n) {
    const float field = (n / 24 % 2 == 0) ? 1.0e6f : -1.0e6f;
    const float out = core.process(field);
    require(std::isfinite(out), "the solver must stay finite on a full-scale square");
    require(std::fabs(out) < 4.0f, "the solver must stay bounded on a full-scale square");
  }

  // A DC step and then silence must settle, not ring or creep.
  for (std::size_t n = 0; n < 100000; ++n) {
    require(std::isfinite(core.process(5.0e4f)), "DC must stay finite");
  }
  for (std::size_t n = 0; n < 100000; ++n) {
    require(std::isfinite(core.process(0.0f)), "silence must stay finite");
  }
}

void testLangevinTaylorSwitchover()
{
  // coth(x) - 1/x is the difference of two diverging terms, so in float it
  // loses every significant bit near zero — it returns exactly 0.0 at x=1e-4.
  // The implementation switches to a Taylor series below a threshold. Both
  // branches are checked against a double-precision reference, because
  // checking the Taylor branch against its own series would prove nothing.
  const auto referenceLangevin = [](double x) { return 1.0 / std::tanh(x) - 1.0 / x; };
  const auto referencePrime = [](double x) {
    const double s = std::sinh(x);
    return 1.0 / (x * x) - 1.0 / (s * s);
  };

  // Walk two decades either side of the seam, so both branches are exercised
  // and the switchover itself is crossed.
  double worstValue = 0.0;
  double worstSlope = 0.0;
  for (int i = 0; i <= 80; ++i) {
    const double x = 1.0e-3 * std::pow(10.0, 3.0 * static_cast<double>(i) / 80.0);
    const float value = ardor::langevin(static_cast<float>(x));
    const float slope = ardor::langevinPrime(static_cast<float>(x));
    require(std::isfinite(value) && std::isfinite(slope), "Langevin must stay finite");

    worstValue = std::max(worstValue,
                          std::fabs(value - referenceLangevin(x)) / std::fabs(referenceLangevin(x)));
    worstSlope = std::max(worstSlope,
                          std::fabs(slope - referencePrime(x)) / std::fabs(referencePrime(x)));
  }
  std::printf("  Langevin worst relative error: %.2e value, %.2e slope\n", worstValue, worstSlope);
  require(worstValue < 1.0e-4, "Langevin must track its double reference across the seam");
  require(worstSlope < 1.0e-4, "Langevin slope must track its double reference across the seam");

  // Pin the seam itself. The log-spaced loop above may step over its immediate
  // neighbourhood, and the seam is the one place a wrong threshold hides: each
  // branch must be accurate at the exact point where the other takes over.
  // Comparing the two sides against each other would only measure the
  // function's own slope, so both are compared against the reference instead.
  // Tolerance is 1e-4 on both sides, the same as the sweep above. The closed
  // form's error near the seam is not smooth: it varies across a band of about
  // one ulp of the intermediate 1/x, which at x=0.1 works out to roughly 3e-5
  // relative. Measured 5.7e-7 on the Taylor side and 2.5e-5 on the closed side.
  // Both are around -90 dB, which is far below anything the magnetics can
  // express, so the seam is inaudible by a wide margin.
  for (const float side : {0.999f, 1.001f}) {
    const double x = static_cast<double>(ardor::kLangevinTaylorLimit) * side;
    const double error = std::fabs(ardor::langevin(static_cast<float>(x)) - referenceLangevin(x))
                       / std::fabs(referenceLangevin(x));
    require(error < 1.0e-4, "both branches must be accurate at the seam");
  }

  // Well away from zero the closed form must still be right: L(x) tends to 1.
  require(std::fabs(ardor::langevin(100.0f) - 0.99f) < 0.02f, "Langevin must saturate at 1");
  require(std::isfinite(ardor::langevinPrime(200.0f)), "Langevin slope must not overflow to NaN");
}

} // namespace

int main()
{
  try {
    testLoopOpens();
    testLoopCloses();
    testOddHarmonicsDominateAndGrow();
    testBoundedUnderAbuse();
    testLangevinTaylorSwitchover();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "tape smoke failed: %s\n", error.what());
    return 1;
  }
  std::printf("tape smoke passed\n");
  return 0;
}
