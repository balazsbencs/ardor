#pragma once

#include "wah/WahNetlist.h"

#include <cstddef>
#include <vector>

namespace ardor {

// Discretized nodal (DK-method) state space for one pot position:
//
//   x[n] = A x[n-1] + B u[n] + C i[n]
//   v[n] = D x[n-1] + E u[n] + F i[n]
//   y[n] = G x[n-1] + H u[n] + K i[n]
//
// with i = f(v) the nonlinear device currents.
//
// The input u is TWO-dimensional: u[0] is the audio sample, u[1] is the 9 V
// supply rail. Carrying the rail as a constant input is what makes the DC
// operating point fall out of the same equations, instead of needing a
// separate bias path bolted on beside them.
//
// All matrices are row-major doubles; the runtime converts to float once, at
// load time.
struct WahDkMatrices {
  std::size_t states = 0;
  std::size_t nonlinearPorts = 0;
  std::size_t inputs = 2;
  std::vector<double> a; // states x states
  std::vector<double> b; // states x inputs
  std::vector<double> c; // states x ports
  std::vector<double> d; // ports x states
  std::vector<double> e; // ports x inputs
  std::vector<double> f; // ports x ports
  std::vector<double> g; // 1 x states
  std::vector<double> h; // 1 x inputs
  std::vector<double> k; // 1 x ports

  // Small-signal base-emitter conductance of each nonlinear port at the DC
  // operating point, Is/(N Vt) exp(Vbe/(N Vt)). Used to linearize the model
  // for analysis, and as the Newton starting point in the offline solve.
  std::vector<double> portConductance; // ports
};

// Builds the MNA system for `netlist` with the pot's grounded divider leg at
// `wiperOhms`, discretizes the reactive elements trapezoidally at
// `sampleRate`, and solves for the state, port, and output rows.
//
// Q1 is linearized about the solved DC operating point; Q2 and Q3 remain
// nonlinear ports. See docs/wah-gcb95-netlist.md for the topology.
WahDkMatrices deriveWahDk(const WahNetlist& netlist, double wiperOhms, double sampleRate);

// Spectral radius of the closed-loop (small-signal linearized) state matrix by
// power iteration. Below 1.0 means the discretized system is stable.
// Analysis helper; not used in the audio path.
double wahSpectralRadius(const WahDkMatrices& matrices);

// Magnitude response of the linearized model at `frequencyHz`, from the audio
// input to the output. Analysis helper; not used in the audio path.
double wahLinearMagnitude(const WahDkMatrices& matrices, double sampleRate, double frequencyHz);

// Frequency of the largest magnitude-response peak, found on a log-spaced grid
// from 50 Hz to just under Nyquist. Analysis helper; not used in the audio
// path.
double wahLinearPeakHz(const WahDkMatrices& matrices, double sampleRate);

} // namespace ardor
