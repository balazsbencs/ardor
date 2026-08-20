#include "rat/RatCircuit.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr double kTwoPi = 6.28318530718;

// Both Newton loops solve a monotonic scalar equation, so the only question is
// how many steps each needs, not whether it converges. The counts differ
// because the two problems do. The op-amp's tanh is gentle and its previous
// output is a good starting point, so two steps reach float precision. The
// diode is an exponential and gains about 16 dB a step, so four steps put the
// error 67 dB below the signal, measured against a converged reference on a
// hard-clipping two-tone. Both counts are fixed, so the per-sample cost cannot
// depend on the signal.
constexpr int kOpampIterations = 2;
constexpr int kDiodeIterations = 4;

// A guard rail on the diode step, not a convergence aid: with the starting
// point chosen below it never binds on real signal, and removing it changes
// nothing measurable. It stays so that a pathological transient cannot send an
// iterate somewhere it would take the rest of the budget to walk back from.
constexpr float kDiodeStepLimitVolts = 0.25f;

// The diode pair and the differential pair are both built from one exponential,
// so each is computed from a single call rather than from two or three library
// functions. On this hardware the transcendentals are the whole cost of the
// model, and the pair of solves below evaluates them eight times per sample.
struct Exponential {
  float positive;   // e^x
  float negative;   // e^-x
};

Exponential exponentials(float x)
{
  // The solution never leaves a volt or so, but a Newton step passing through
  // an extreme value must not overflow on the way. Saturating the argument
  // leaves the function monotonic, which is all the solve needs.
  const float clamped = std::clamp(x, -80.0f, 80.0f);
  const float positive = std::exp(clamped);
  return {positive, 1.0f / positive};
}

// A one-pole coefficient for a corner at `hz`.
float onePoleCoefficient(double hz, double sampleRate)
{
  if (!(hz > 0.0) || !(sampleRate > 0.0)) return 1.0f;
  const double coefficient = 1.0 - std::exp(-6.28318530718 * hz / sampleRate);
  return static_cast<float>(std::clamp(coefficient, 0.0, 1.0));
}

} // namespace

void RatCircuit::init(const RatNetlist& netlist, float sampleRate)
{
  netlist_ = netlist;
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 192000.0f;
  step_ = 1.0f / sampleRate_;

  const double rate = static_cast<double>(sampleRate_);

  // Input network. C3 into R2 blocks DC; R1 into C2 keeps radio out.
  inputHighPassCoeff_ = onePoleCoefficient(
    1.0 / (kTwoPi * netlist_.r2Ohms * netlist_.c3Farads), rate);
  inputLowPassCoeff_ = onePoleCoefficient(
    1.0 / (kTwoPi * netlist_.r1Ohms * netlist_.c2Farads), rate);

  // Output network. Both are DC blocks; neither shapes the audible band.
  gateHighPassCoeff_ = onePoleCoefficient(
    1.0 / (kTwoPi * netlist_.r12Ohms * netlist_.c12Farads), rate);
  outputHighPassCoeff_ = onePoleCoefficient(
    1.0 / (kTwoPi * netlist_.r14Ohms * netlist_.c13Farads), rate);

  // Trapezoidal companion conductances, all independent of the knobs.
  const double halfStep = 0.5 / rate;
  c9Conductance_ = static_cast<float>(netlist_.c9Farads / halfStep);
  c7Conductance_ = static_cast<float>(netlist_.c7Farads / halfStep);
  c8Conductance_ = static_cast<float>(netlist_.c8Farads / halfStep);
  c11Conductance_ = static_cast<float>(netlist_.c11Farads / halfStep);
  c10Conductance_ = static_cast<float>(netlist_.c10Farads / halfStep);

  // A resistor in series with a capacitor collapses to one conductance and one
  // history voltage: the current is (node - history) / (R + 1/Gc).
  leg7Conductance_ = static_cast<float>(
    1.0 / (netlist_.r7Ohms + halfStep / netlist_.c7Farads));
  leg8Conductance_ = static_cast<float>(
    1.0 / (netlist_.r8Ohms + halfStep / netlist_.c8Farads));
  clipInputConductance_ = static_cast<float>(
    1.0 / (netlist_.r10Ohms + halfStep / netlist_.c10Farads));

  // One expression covers both op-amp limits: the slope at zero is 2*pi*gbw and
  // the saturated rate is the slew rate.
  slewPerStep_ = static_cast<float>(netlist_.slewVoltsPerSec / rate);
  tanhGain_ = static_cast<float>(kTwoPi * netlist_.gbwHz / netlist_.slewVoltsPerSec);

  diodeScale_ = static_cast<float>(2.0 * netlist_.diodeSaturationCurrent);
  diodeInverseVt_ = static_cast<float>(
    1.0 / (netlist_.diodeEmissionCoefficient * netlist_.diodeThermalVolts));

  refreshCoefficients();
  reset();
}

void RatCircuit::reset()
{
  inputHighPassState_ = 0.0f;
  inputLowPassState_ = 0.0f;
  leg7History_ = 0.0f;
  leg8History_ = 0.0f;
  c9History_ = 0.0f;
  opampOut_ = 0.0f;
  c10History_ = 0.0f;
  c11History_ = 0.0f;
  gateHighPassState_ = 0.0f;
  outputHighPassState_ = 0.0f;
}

void RatCircuit::setControls(float distortion, float filter, float volume)
{
  distortion = std::clamp(distortion, 0.0f, 1.0f);
  filter = std::clamp(filter, 0.0f, 1.0f);
  volume = std::clamp(volume, 0.0f, 1.0f);
  if (distortion == distortion_ && filter == filter_ && volume == volume_) return;
  distortion_ = distortion;
  filter_ = filter;
  volume_ = volume;
  refreshCoefficients();
}

void RatCircuit::refreshCoefficients()
{
  const double distortionOhms = ratPotOhms(netlist_, netlist_.r9Ohms, distortion_);
  // At the bottom of the travel the feedback resistor is a short, which is a
  // unity-gain follower and an infinite conductance. Floor it at an ohm; the
  // stage is already at unity there and the difference is inaudible.
  gainFeedbackConductance_ = static_cast<float>(1.0 / std::max(distortionOhms, 1.0));
  feedbackDivisor_ = gainFeedbackConductance_ + c9Conductance_
    + leg7Conductance_ + leg8Conductance_;

  const double filterOhms = ratPotOhms(netlist_, netlist_.r17Ohms, filter_) + netlist_.r15Ohms;
  filterConductance_ = static_cast<float>(1.0 / filterOhms);
  toneFromClip_ = filterConductance_ / (filterConductance_ + c11Conductance_);
  clipDivisor_ = clipInputConductance_ + filterConductance_ * (1.0f - toneFromClip_);

  volumeGain_ = static_cast<float>(ratPotOhms(netlist_, 1.0, volume_));
}

float RatCircuit::process(float input)
{
  const float sample = std::isfinite(input) ? input : 0.0f;

  // --- Input network -----------------------------------------------------
  inputHighPassState_ += inputHighPassCoeff_ * (sample - inputHighPassState_);
  const float coupled = sample - inputHighPassState_;
  inputLowPassState_ += inputLowPassCoeff_ * (coupled - inputLowPassState_);
  const float nonInverting = inputLowPassState_;

  // --- Gain stage --------------------------------------------------------
  // The inverting input is a linear function of the op-amp output and the
  // capacitor histories, so the loop reduces to one scalar equation.
  const float invertingSlope =
    (gainFeedbackConductance_ + c9Conductance_) / feedbackDivisor_;
  const float invertingOffset =
    (leg7Conductance_ * leg7History_ + leg8Conductance_ * leg8History_
     - c9Conductance_ * c9History_) / feedbackDivisor_;

  // Backward Euler on dV/dt = slew * tanh(gain * (Vp - Vm)), solved for V.
  // The residual is strictly increasing in V, so Newton cannot walk away.
  float out = opampOut_;
  const float previousOut = opampOut_;
  for (int iteration = 0; iteration < kOpampIterations; ++iteration) {
    const float differential = nonInverting - (invertingSlope * out + invertingOffset);
    const auto pair = exponentials(tanhGain_ * differential);
    const float saturated =
      (pair.positive - pair.negative) / (pair.positive + pair.negative);
    const float residual = out - previousOut - slewPerStep_ * saturated;
    const float derivative =
      1.0f + slewPerStep_ * tanhGain_ * invertingSlope * (1.0f - saturated * saturated);
    out -= residual / derivative;
  }
  const float swing = static_cast<float>(netlist_.outputSwingVolts);
  opampOut_ = std::clamp(out, -swing, swing);

  // Settle the feedback network on the output the op-amp actually reached, so
  // the histories stay consistent with a clipped stage rather than with the
  // unclipped solution.
  const float inverting = invertingSlope * opampOut_ + invertingOffset;
  const float leg7Current = (inverting - leg7History_) * leg7Conductance_;
  const float leg8Current = (inverting - leg8History_) * leg8Conductance_;
  leg7History_ = inverting - leg7Current * static_cast<float>(netlist_.r7Ohms)
    + leg7Current / c7Conductance_;
  leg8History_ = inverting - leg8Current * static_cast<float>(netlist_.r8Ohms)
    + leg8Current / c8Conductance_;
  c9History_ = 2.0f * (opampOut_ - inverting) - c9History_;

  // --- Clipping and filter ----------------------------------------------
  // Vtone is linear in Vclip, so substituting it leaves one scalar equation in
  // Vclip. The residual is strictly decreasing, so again Newton is safe.
  const float drive = opampOut_ - c10History_;
  const float toneOffset = c11Conductance_ * c11History_
    / (filterConductance_ + c11Conductance_);
  // Start from where the node would sit with the diodes removed, pulled back to
  // where a conducting diode can hold it. Starting from the previous sample is
  // far worse: at 192 kHz the op-amp ahead of this node can move more than a
  // volt in a single step, so its last value can be nowhere near.
  const float openCircuit =
    (clipInputConductance_ * drive + filterConductance_ * toneOffset) / clipDivisor_;
  float clip = std::clamp(openCircuit, -0.7f, 0.7f);
  for (int iteration = 0; iteration < kDiodeIterations; ++iteration) {
    const auto pair = exponentials(clip * diodeInverseVt_);
    const float diodeCurrent = 0.5f * diodeScale_ * (pair.positive - pair.negative);
    const float diodeSlope =
      0.5f * diodeScale_ * diodeInverseVt_ * (pair.positive + pair.negative);
    const float residual = clipInputConductance_ * (drive - clip)
      - filterConductance_ * ((1.0f - toneFromClip_) * clip - toneOffset)
      - diodeCurrent;
    const float derivative = -clipDivisor_ - diodeSlope;
    const float delta = std::clamp(residual / derivative,
                                   -kDiodeStepLimitVolts, kDiodeStepLimitVolts);
    clip -= delta;
  }

  const float tone = toneFromClip_ * clip + toneOffset;
  // The series branch stores the capacitor voltage, not the node voltage, so
  // the resistor drop has to come out first.
  const float clipInputCurrent = clipInputConductance_ * (drive - clip);
  const float c10Volts = opampOut_ - clip
    - clipInputCurrent * static_cast<float>(netlist_.r10Ohms);
  c10History_ = c10Volts + clipInputCurrent / c10Conductance_;
  c11History_ = 2.0f * tone - c11History_;

  // --- Output ------------------------------------------------------------
  gateHighPassState_ += gateHighPassCoeff_ * (tone - gateHighPassState_);
  const float gate = tone - gateHighPassState_;
  outputHighPassState_ += outputHighPassCoeff_ * (gate - outputHighPassState_);
  const float buffered = gate - outputHighPassState_;

  const float result = buffered * volumeGain_;
  return std::isfinite(result) ? result : 0.0f;
}

} // namespace ardor
