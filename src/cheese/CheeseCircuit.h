#pragma once

#include "cheese/CheeseDk.h"
#include "cheese/CheeseNetlist.h"

#include <array>
#include <vector>

namespace ardor {

// Runtime model of the Big Cheese, evaluated one sample at a time at the
// OVERSAMPLED rate. It knows nothing about oversampling; CheeseProcessor owns
// that. Signals are volts, one volt in being about as hot as a humbucker gets,
// the same convention the wah and the RAT models use.
//
// Three sections, split where the circuit itself splits:
//
//   1. The input network into the buffer. The buffer is an op-amp follower, so
//      it draws nothing from the network and presents a source to what follows.
//      Two one-pole filters.
//
//   2. Everything from there to the output stage's input, solved as one nodal
//      system by the DK method. It has to be one system: the fuzz pair is a
//      feedback loop, the clipping node loads the fuzz through 47 kOhm against
//      its own 10 kOhm collector load, and the tone stack loads the clipper.
//      Three nonlinear ports, solved by damped Newton each sample.
//
//   3. The output stage. Its op-amp input draws nothing, so section 2 does not
//      see it. The AD712 there is fast enough to treat as ideal at these
//      levels, so this section is linear.
//
// setControls() rebuilds the matrices, which costs a matrix factorisation. It
// is cheap enough for a control-rate call but must not run per sample.
class CheeseCircuit {
public:
  void init(const CheeseNetlist& netlist, float sampleRate);
  void reset();

  // Knob positions, 0..1.
  void setControls(float fuzz, float tone, float volume);

  float process(float input);

  // The clipping node, in volts. Exposed so tests can watch where the diodes
  // hold it rather than only seeing what comes out of the tone stack.
  float clipperVolts() const noexcept { return clipperVolts_; }

  // How many samples have left the Newton at its iteration cap without
  // converging. On musical material this stays at zero, and a test holds it
  // there. It is worth having at runtime too: if a change to the circuit or the
  // limiting makes the solve struggle, this is what says so, rather than a
  // listener eventually noticing crackle.
  unsigned long long unconvergedSamples() const noexcept { return unconverged_; }

private:
  void rebuild();

  CheeseNetlist netlist_{};
  CheeseDkMatrices matrices_{};
  float sampleRate_ = 192000.0f;

  float fuzz_ = 0.7f;
  float tone_ = 0.5f;
  float volume_ = 0.7f;
  float volumeGain_ = 1.0f;
  bool dirty_ = true;

  // --- Input network -----------------------------------------------------
  float inputHighPassCoeff_ = 0.0f;
  float inputLowPassCoeff_ = 1.0f;
  float inputHighPassState_ = 0.0f;
  float inputLowPassState_ = 0.0f;

  // --- Nodal section -----------------------------------------------------
  //
  // Float is enough. The state was carried in double for a while, on the theory
  // that a near-unity mode would integrate the solve's residual into an audible
  // drift. It does not: with the limiting below fixed, twenty seconds of hard
  // playing leaves the output where it started, and the harmonics agree with a
  // double-precision run to four decimal places.
  std::vector<float> state_;
  std::vector<float> scratch_;
  std::array<float, 3> portVolts_{};
  // Where each port starts being limited, derived from how hard it drives
  // the network rather than from the device on its own.
  std::array<float, 3> critical_{};
  float clipperVolts_ = 0.0f;
  unsigned long long unconverged_ = 0;

  // --- Output stage ------------------------------------------------------
  float feedbackSlope_ = 1.0f;
  float feedbackConductance_ = 0.0f;   // R17 in parallel with C10
  float c10Conductance_ = 0.0f;
  float leg18Conductance_ = 0.0f;      // R18 in series with C11
  float c11Conductance_ = 0.0f;
  float c10History_ = 0.0f;
  float leg18History_ = 0.0f;
  float outputHighPassCoeff_ = 0.0f;
  float outputHighPassState_ = 0.0f;
  float outputDivider_ = 1.0f;         // R19 against the volume track
};

} // namespace ardor
