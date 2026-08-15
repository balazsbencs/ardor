#pragma once

#include "cheese/CheeseDk.h"
#include "cheese/CheeseNetlist.h"

#include <array>
#include <vector>

namespace ardor {

// NOT YET WIRED INTO THE CHAIN. The model is derived and behaves correctly —
// the operating point is physical, the fuzz compresses with playing level, the
// clipper holds at a silicon drop and the tone stack sweeps — but the runtime
// solve needs sixteen Newton steps per sample to stay bounded, which is about
// 1.2 us per oversampled sample. That is several times the whole RAT block and
// well outside what fits alongside an amp model. The cause is written up at the
// iteration count in the implementation. Until that is fixed there is no block
// definition and nothing in either browser.
//
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
  // Raised only while reset() charges the bias network, which is not real time.
  bool settling_ = false;

  // --- Input network -----------------------------------------------------
  float inputHighPassCoeff_ = 0.0f;
  float inputLowPassCoeff_ = 1.0f;
  float inputHighPassState_ = 0.0f;
  float inputLowPassState_ = 0.0f;

  // --- Nodal section -----------------------------------------------------
  //
  // The state is kept in double where everything else here is float. It is an
  // accumulator: the state matrix sits at a spectral radius of 0.9998, so one
  // of its modes barely decays, and the input coupling has entries in the
  // hundreds. A residual of a few microamps, which is all the float noise floor
  // allows the solve to reach, then integrates into volts over a second of
  // audio. In double it stays far below anything audible.
  std::vector<double> state_;
  std::vector<double> scratch_;
  std::array<float, 3> portVolts_{};
  float clipperVolts_ = 0.0f;

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
