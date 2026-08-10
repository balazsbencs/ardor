#pragma once

namespace ardor {

// Component values and topology for one wah voicing. Resistances in ohms,
// capacitances in farads, inductance in henries. Doubles throughout: this
// feeds the offline matrix derivation, where float rounding in a matrix
// inverse is not worth the risk. Runtime code consumes the derived matrices as
// floats, not this struct.
//
// Field names carry the schematic designators from the source cited in
// WahNetlist.cpp. Do not rename them to something more "semantic" — the
// designator is what makes a value checkable against the schematic.
//
// Node names below match docs/wah-gcb95-netlist.md, which records the full
// extracted connectivity. Read that file before touching the DK derivation.
struct WahNetlist {
  // --- Input stage: Q1, MPSA13 Darlington emitter follower ---------------
  //
  // Modelled as LINEAR. A Darlington follower at near-unity gain doing
  // impedance isolation contributes essentially nothing to the wah voicing,
  // which comes from the Q2 filter stage and the Q3 feedback path. Treating it
  // as nonlinear would make the solved system 3-D and the runtime table 4-D —
  // roughly 800 MB at the shipped grid. Same tradeoff as omitting the
  // base-collector junctions.
  double c3Farads = 10e-9;      // Input coupling, source -> Vin.
  double r2Ohms = 1e6;          // Vin bias, to Q1 collector node n3.
  double r3Ohms = 2.2e6;        // Vin bias, to ground.
  double c6Farads = 22e-12;     // Vin to ground; input bandwidth limit.
  double r1Ohms = 1000.0;       // Q1 collector to the 9 V rail.
  double r4Ohms = 10000.0;      // Q1 emitter (n7) to ground.
  double q1Beta = 5000.0;       // MPSA13 Darlington.

  // --- Active filter: Q2, MPSA18 common emitter --------------------------
  double r5Ohms = 68000.0;      // Q1 emitter n7 -> n2.
  double c2Farads = 10e-9;      // n2 -> n1 (Q2 base).
  double r10Ohms = 1500.0;      // Tank node n5 -> n1.
  double r6Ohms = 22000.0;      // Q2 collector n8 to the 9 V rail.
  double r7Ohms = 390.0;        // Q2 emitter n9 to ground; sets stage gain.

  // --- Resonant tank -----------------------------------------------------
  // L1 sits in PARALLEL with R9 between n6 and n5; R9 is the dominant damping
  // term, not the winding resistance.
  double inductorHenries = 0.5;     // L1, 500 mH, n6 <-> n5.
  double inductorSeriesOhms = 40.0; // L1 winding DC resistance.
  double r9Ohms = 33000.0;          // n6 <-> n5, parallel with L1.
  double r8Ohms = 470000.0;         // Q2 collector n8 -> n6.
  double c11Farads = 4.7e-6;        // n6 -> ground.
  double r16Ohms = 82000.0;         // n6 -> ground.

  // --- Output ------------------------------------------------------------
  double c1Farads = 0.22e-6;    // Q2 collector n8 -> Vout.

  // --- Feedback follower: Q3, MPSA18 -------------------------------------
  // The pot is a 100k divider from Vout to ground tapped at Vfb_in; the tap
  // drives Q3, whose emitter feeds C7 back into the tank node n5. This loop is
  // what sweeps the resonance, and it is why Q and peak gain rise together.
  double potOhms = 100000.0;    // R12 + R14 total track.
  double c8Farads = 0.22e-6;    // Vfb_in -> Q3 base n10.
  double r11Ohms = 470000.0;    // n10 -> Q2 collector n8; Q3 base bias.
  double r18Ohms = 1000.0;      // Q3 collector n11 to the 9 V rail.
  double r19Ohms = 10000.0;     // Q3 emitter Vfb to ground.
  double c7Farads = 10e-9;      // Q3 emitter Vfb -> tank node n5.

  // Shared MPSA18 model for Q2 and Q3. Ebers-Moll, forward-active, BE junction
  // only — the base-collector junctions are deliberately omitted, which is what
  // keeps the solved system 2-D instead of 4-D.
  double bjtSaturationCurrent = 1e-14;
  double bjtEmissionCoefficient = 1.0;
  double bjtThermalVolts = 0.02585;
  double bjtBeta = 500.0;       // MPSA18 is a high-hFE part.

  double supplyVolts = 9.0;

  // Audio-taper approximation exponent: the grounded leg R12 of the divider is
  // potOhms * position^exponent, and R14 is the remainder of the track.
  double taperExponent = 2.4;
};

const WahNetlist& gcb95Netlist();

// Rejects netlists that are not physically realizable. Every component value
// must be positive and finite.
bool wahNetlistValid(const WahNetlist& netlist);

// Maps treadle position (0 = heel, 1 = toe) to the wiper resistance of the
// grounded divider leg (R12). Positions outside 0..1 clamp.
double wahPotWiperOhms(const WahNetlist& netlist, double position);

} // namespace ardor
