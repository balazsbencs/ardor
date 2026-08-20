#pragma once

#include "rat/RatNetlist.h"

namespace ardor {

// Runtime model of the RAT, evaluated one sample at a time at the OVERSAMPLED
// rate. It knows nothing about oversampling; RatProcessor owns that.
//
// Signals are volts. A sample of 1.0 is one volt at the input jack, which is
// about as hot as a humbucker gets, and the same convention the wah model uses.
//
// The circuit is solved in three pieces rather than as one nodal system,
// because the RAT's own topology separates them and the separations are exact
// rather than convenient:
//
//   1. The input network drives the op-amp's non-inverting input, which draws
//      no current. Nothing downstream loads it.
//
//   2. The gain stage ends at an op-amp output, which is a voltage source.
//      Nothing downstream loads it either. Inside the stage the feedback
//      network and the op-amp are solved together, because they are a loop.
//
//   3. The clipping node and the filter must be solved together, because the
//      filter genuinely loads the diodes: its impedance falls to R15, 1.5 kOhm,
//      which is the same order as the 1 kOhm feeding the node. Solving them
//      separately would overstate the clipping at high frequencies.
//
//   4. The output follower's gate draws no current, so the filter does not see
//      it, and the follower itself is linear.
//
// Two nonlinear solves run per sample, one per loop, each a scalar Newton
// iteration on a monotonic function. Both are bounded to a fixed iteration
// count, so the per-sample cost has no data-dependent loop and is safe in a
// SCHED_FIFO callback.
class RatCircuit {
public:
  void init(const RatNetlist& netlist, float sampleRate);
  void reset();

  // All three are knob positions, 0..1.
  //   distortion  0 gives unity gain, 1 gives about 67 dB.
  //   filter      0 is brightest. Higher is darker, as on the pedal, where the
  //               control works the opposite way round to a normal tone knob.
  //   volume      output level.
  void setControls(float distortion, float filter, float volume);

  float process(float input);

  // Op-amp output in volts, before the clipping stage. Exposed for tests, which
  // need to see the gain stage on its own.
  float stageOutputVolts() const noexcept { return opampOut_; }

private:
  void refreshCoefficients();

  RatNetlist netlist_{};
  float sampleRate_ = 192000.0f;
  float step_ = 1.0f / 192000.0f;

  float distortion_ = 0.5f;
  float filter_ = 0.5f;
  float volume_ = 1.0f;
  float volumeGain_ = 1.0f;

  // --- Input network -----------------------------------------------------
  float inputHighPassCoeff_ = 0.0f;
  float inputLowPassCoeff_ = 1.0f;
  float inputHighPassState_ = 0.0f;   // running average that is subtracted off
  float inputLowPassState_ = 0.0f;

  // --- Gain stage --------------------------------------------------------
  // Trapezoidal companions. The two series legs collapse to a conductance and a
  // history voltage each; C9 across the feedback resistor stays explicit.
  float gainFeedbackConductance_ = 0.0f;   // 1 / R9
  float c9Conductance_ = 0.0f;
  float leg7Conductance_ = 0.0f;
  float leg8Conductance_ = 0.0f;
  float c7Conductance_ = 0.0f;
  float c8Conductance_ = 0.0f;
  float feedbackDivisor_ = 1.0f;           // sum of the four above

  float leg7History_ = 0.0f;
  float leg8History_ = 0.0f;
  float c9History_ = 0.0f;
  float opampOut_ = 0.0f;

  // dV/dt = slew * tanh(tanhGain * differentialInput). The small-signal slope
  // is 2*pi*gbw and the large-signal limit is the slew rate; one expression
  // gives both, with the differential pair supplying the transition.
  float slewPerStep_ = 0.0f;
  float tanhGain_ = 0.0f;

  // --- Clipping and filter ----------------------------------------------
  float clipInputConductance_ = 0.0f;   // series C10 + R10
  float filterConductance_ = 0.0f;      // 1 / (R17 + R15)
  float c10Conductance_ = 0.0f;
  float c11Conductance_ = 0.0f;
  float toneFromClip_ = 0.0f;           // filter output as a fraction of Vclip
  float clipDivisor_ = 1.0f;

  float c10History_ = 0.0f;
  float c11History_ = 0.0f;

  float diodeScale_ = 0.0f;             // 2 * Is
  float diodeInverseVt_ = 0.0f;         // 1 / (n * Vt)

  // --- Output ------------------------------------------------------------
  float gateHighPassCoeff_ = 0.0f;
  float outputHighPassCoeff_ = 0.0f;
  float gateHighPassState_ = 0.0f;
  float outputHighPassState_ = 0.0f;
};

} // namespace ardor
