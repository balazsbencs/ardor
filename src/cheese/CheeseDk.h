#pragma once

#include "cheese/CheeseNetlist.h"

#include <cstddef>
#include <vector>

namespace ardor {

// Knob positions, 0..1, that change the matrices. The volume pot is outside
// this system, so it is not here.
struct CheeseControls {
  double fuzz = 0.7;
  double tone = 0.5;
};

// Discrete state space for the whole nonlinear section: from the input buffer's
// output through the fuzz pair, the clipping node and the tone stack, to the
// output stage's input.
//
//   x[n+1] = A x[n] + B u[n] + C i[n]
//   v[n]   = D x[n] + E u[n] + F i[n]
//   y[n]   = G x[n] + H u[n] + K i[n]
//
// where u is (audio, supply rail), i is the current drawn by each nonlinear
// port and v the voltage across it. The runtime solves v = f(i) by Newton each
// sample; everything else is a matrix product.
//
// The three ports are, in order: Q1's base-emitter junction, Q2's base-emitter
// junction, and the clipping node. The first two carry a base current, so their
// collector and emitter currents follow from beta. The third is a plain node
// current.
struct CheeseDkMatrices {
  std::size_t states = 0;
  std::size_t ports = 0;
  std::size_t inputs = 0;

  std::vector<double> a, b, c;
  std::vector<double> d, e, f;
  std::vector<double> g, h, k;

  // Small-signal conductance of each port at the operating point. The runtime
  // uses it to seed its Newton, and it is what a linear analysis linearizes to.
  std::vector<double> portConductance;
  // Port voltages at the operating point. The Newton starts here after a reset,
  // which matters because a cold start on an exponential is where these solves
  // go wrong.
  std::vector<double> portVoltage;
  // Output at the operating point, subtracted at runtime so the block passes
  // audio rather than audio riding on half the supply.
  double outputOffset = 0.0;
};

// Solves the DC operating point and derives the matrices for one control
// setting. Both are offline-cost work: the operating point runs a damped Newton
// to convergence and must not be called from the audio thread.
CheeseDkMatrices deriveCheeseDk(const CheeseNetlist& netlist, const CheeseControls& controls,
                                double sampleRate);

// The operating point on its own, exposed so it can be checked against what the
// circuit is supposed to do rather than only against itself. Indexed by the
// node enumeration in the implementation; use cheeseNodeVoltage to read it.
std::vector<double> cheeseOperatingPoint(const CheeseNetlist& netlist,
                                         const CheeseControls& controls);

// Named accessors for the operating point, so tests do not depend on the node
// numbering.
double cheeseQ1CollectorVolts(const std::vector<double>& operatingPoint);
double cheeseQ2CollectorVolts(const std::vector<double>& operatingPoint);
double cheeseQ1BaseEmitterVolts(const std::vector<double>& operatingPoint);
double cheeseQ2BaseEmitterVolts(const std::vector<double>& operatingPoint);

} // namespace ardor
