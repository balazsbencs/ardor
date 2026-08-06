#pragma once

namespace ardor {

// Component values for one wah voicing. Resistances in ohms, capacitances in
// farads, inductance in henries. Doubles throughout: this feeds the offline
// matrix derivation, where float rounding in a matrix inverse is not worth the
// risk. Runtime code consumes the derived matrices as floats, not this struct.
//
// Field names carry the schematic designators so values stay traceable to the
// source cited in WahNetlist.cpp. Do not rename them to something more
// "semantic" — the designator is the whole point.
struct WahNetlist {
  // Input network.
  double cin1Farads = 10e-9;    // 0.01 uF input coupling.
  double r1Ohms = 68000.0;      // Input series resistor.

  // Q0: MPSA13 Darlington emitter follower, the input buffer.
  //
  // Modelled as LINEAR. A Darlington follower at near-unity gain doing
  // impedance isolation contributes essentially nothing to the wah voicing,
  // which comes from the Q1 feedback stage. Treating it as nonlinear would
  // make the solved system 3-D and the runtime table 4-D — roughly 800 MB at
  // the shipped grid. Same tradeoff as omitting the base-collector junctions.
  double r5Ohms = 470000.0;     // Q0 bias.
  double r6Ohms = 470000.0;     // Q0 bias.
  double r9Ohms = 1000.0;       // Q0 emitter.
  double q0Beta = 5000.0;       // MPSA13 Darlington.

  // Q1: MPSA18 common-emitter gain stage driving the resonant tank.
  double r2Ohms = 1500.0;
  double r3Ohms = 22000.0;
  double r4Ohms = 390.0;        // Q1 emitter; sets stage gain.
  double c3Farads = 4.7e-6;     // Feedback coupling from the output stage.

  // Resonant tank.
  double inductorHenries = 0.5;     // L1, 500 mH.
  double inductorSeriesOhms = 40.0; // Winding DC resistance; damps the peak.
  double c1Farads = 10e-9;          // 0.01 uF.
  double c2Farads = 10e-9;          // 0.01 uF.
  double potOhms = 100000.0;        // VR1, 100k.

  // Q2: MPSA18 output stage. The pot sits in the feedback path from here.
  double r7Ohms = 33000.0;
  double r8Ohms = 82000.0;
  double r10Ohms = 10000.0;
  double c4Farads = 0.22e-6;
  double c5Farads = 0.22e-6;
  double c7Farads = 0.1e-6;     // Output coupling.

  // Shared MPSA18 model for Q1 and Q2. Ebers-Moll, forward-active, BE junction
  // only — the base-collector junctions are deliberately omitted, which is what
  // keeps the solved system 2-D instead of 4-D.
  double bjtSaturationCurrent = 1e-14;
  double bjtEmissionCoefficient = 1.0;
  double bjtThermalVolts = 0.02585;
  double bjtBeta = 500.0;       // MPSA18 is a high-hFE part.

  double supplyVolts = 9.0;

  // Audio-taper approximation exponent: wiper = potOhms * position^exponent.
  // Larger means more of the sweep is concentrated toward the toe.
  double taperExponent = 2.4;
};

const WahNetlist& gcb95Netlist();

// Rejects netlists that are not physically realizable. Every component value
// must be positive and finite.
bool wahNetlistValid(const WahNetlist& netlist);

// Maps treadle position (0 = heel, 1 = toe) to wiper resistance. Positions
// outside 0..1 clamp.
double wahPotWiperOhms(const WahNetlist& netlist, double position);

} // namespace ardor
