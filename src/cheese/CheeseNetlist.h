#pragma once

namespace ardor {

// Component values for the Lovetone Big Cheese. Resistances in ohms,
// capacitances in farads. Doubles throughout, because these feed the offline
// matrix derivation where float rounding in a matrix inverse is not worth the
// risk; the runtime consumes the derived matrices as floats, not this struct.
//
// Field names carry the schematic designators from docs/big-cheese-netlist.md,
// which records the full extracted connectivity and, importantly, where the
// source is incomplete. Read that document before touching this one.
struct CheeseNetlist {
  // --- Input network, ahead of the buffer --------------------------------
  // C3 into the bias divider is a 10 Hz high pass; R2 into C4 is a 106 kHz low
  // pass. R1 sits across the source and does nothing to the signal.
  double c3Farads = 47e-9;
  double r3Ohms = 680000.0;     // n4 -> Va.
  double r4Ohms = 680000.0;     // n4 -> ground.
  double r2Ohms = 1000.0;       // n3 -> n4.
  double c4Farads = 1.5e-9;     // n4 -> ground.

  // --- Fuzz pair ---------------------------------------------------------
  // Q1 and Q2 are one feedback loop, not two stages: R5 runs from Q2's emitter
  // back to Q1's base. They share a bias point, which is why this family of
  // circuit cleans up from the guitar volume the way it does.
  double c5Farads = 47e-9;      // Buffer -> Q1 base.
  double r5Ohms = 100000.0;     // Q1 base <- Q2 emitter. The loop.
  double r6Ohms = 100000.0;     // Va -> Q1 collector.
  double r8Ohms = 470.0;        // Q1 emitter -> ground.
  double r9Ohms = 470000.0;     // n11 -> Q1 emitter.
  double c7Farads = 47e-9;      // Q2 collector -> n11.
  double c6Farads = 47e-12;     // Q2 collector -> Q2 base.
  double r7Ohms = 10000.0;      // Va -> Q2 collector.

  // The Fuzz pot is a 1 k track from Q2's emitter to ground with C9 across its
  // lower leg. Because C9 blocks DC, the bias sees the whole track whatever the
  // wiper does, and only the AC gain changes. That is what lets a fuzz of this
  // family be turned down without going out of bias.
  double fuzzTrackOhms = 1000.0;
  double c9Farads = 4.7e-6;

  // The Trim pot is NOT modelled, and its values are not here. In the source
  // schematic its far leg connects to nothing, so as drawn it does nothing;
  // docs/big-cheese-netlist.md records that. Wiring it to ground would parallel
  // the Fuzz pot's lower leg, and that was tried — but it can only lower the
  // pair's DC emitter resistance, which drives Q2 towards saturation. The
  // device model below carries the base-emitter junction only, so it has no
  // saturation to reach: past about half travel the solved collector voltage
  // goes below ground, which is not a bias setting, it is the model leaving its
  // range. It is also the wrong direction, since gating in this family of
  // circuit comes from starving a transistor rather than saturating it.

  // --- Clipping ----------------------------------------------------------
  double c8Farads = 47e-9;      // Q2 collector -> n13.
  double r12Ohms = 47000.0;     // n13 -> the clipping node.
  // D2 conducts out of the node. Q3 has its collector tied to its emitter with
  // its base at ground, which puts two junctions in parallel conducting into
  // it. Symmetric-looking parts, an asymmetric curve.
  double diodeSaturationCurrent = 2.52e-9;
  double diodeEmissionCoefficient = 1.752;
  double clipperJunctionCount = 2.0;   // Q3's base-emitter and base-collector.

  // --- Tone stack, Big Muff shape ---------------------------------------
  double r22Ohms = 47000.0;
  double r23Ohms = 47000.0;
  double c14Farads = 2.2e-9;
  double c15Farads = 6.8e-9;
  double r24Ohms = 47000.0;
  double r25Ohms = 47000.0;
  double c16Farads = 10e-9;
  double toneTrackOhms = 100000.0;

  // --- Output stage ------------------------------------------------------
  double c12Farads = 47e-9;     // Tone wiper -> the output stage input.
  double r15Ohms = 680000.0;    // n17 -> Va.
  double r16Ohms = 680000.0;    // n17 -> ground.
  // The op-amp here is absent from the source drawing; only its supply stubs
  // survive. This is the arrangement the surrounding parts imply and the one
  // the published description calls output gain recovery. Reconstruction.
  double r17Ohms = 33000.0;     // Feedback.
  double c10Farads = 100e-12;   // Across the feedback resistor.
  double r18Ohms = 15000.0;     // Inverting input -> C11.
  double c11Farads = 100e-9;    // -> ground. Sets where the gain starts rising.
  double r19Ohms = 470.0;       // Output stage -> the volume track.
  double c13Farads = 4.7e-6;    // DC block ahead of the volume pot.
  double volumeTrackOhms = 10000.0;

  // --- Devices -----------------------------------------------------------
  // 2N3904. Base-emitter junction only, forward active, as in the wah: the
  // base-collector junctions are deliberately omitted, which is what keeps the
  // solved system three-dimensional instead of five.
  double bjtSaturationCurrent = 6.734e-15;
  double bjtEmissionCoefficient = 1.0;
  double bjtBeta = 200.0;

  // One temperature, so one thermal voltage for every junction in the circuit.
  double thermalVolts = 0.02585;

  double supplyVolts = 9.0;

  // Audio-taper approximation exponent, shared by the pots: the wiper
  // resistance is the full track times position raised to this power.
  double taperExponent = 2.4;
};

const CheeseNetlist& cheeseNetlist();

// Rejects netlists that are not physically realizable. Every component value
// must be positive and finite.
bool cheeseNetlistValid(const CheeseNetlist& netlist);

} // namespace ardor
