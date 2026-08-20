#pragma once

namespace ardor {

// Component values for the ProCo RAT. Resistances in ohms, capacitances in
// farads. Doubles throughout, because these feed coefficient derivation where
// float rounding is not worth the risk; the runtime consumes the derived
// coefficients as floats, not this struct.
//
// Field names carry the schematic designators from
// docs/rat-distortion-netlist.md, which records the full extracted
// connectivity. Do not rename them to something more "semantic" — the
// designator is what makes a value checkable against the schematic. Read that
// document before touching this one.
struct RatNetlist {
  // --- Input network -----------------------------------------------------
  // C3 into R2 is a 7.2 Hz high pass; R1 into C2 is a 159 kHz low pass that
  // keeps radio out of a very high gain stage. R3 sits across the source and
  // does nothing to the signal.
  double c3Farads = 22e-9;      // Input coupling, Vin -> n5.
  double r2Ohms = 1e6;          // Bias feed, V4P5 -> n5.
  double r1Ohms = 1000.0;       // n5 -> n2, the non-inverting input.
  double c2Farads = 1e-9;       // n2 -> ground.

  // --- Gain stage --------------------------------------------------------
  // Non-inverting, gain = 1 + Zf / Z, where Zf is R9 in parallel with C9 and Z
  // is the two series legs below in parallel. Both legs are capacitively
  // coupled, so the gain is unity at DC and climbs with frequency. That tilt
  // is the RAT's voice: the diodes are handed a signal that is already shaped.
  double r9Ohms = 100000.0;     // Distortion pot, Vfb -> n1.
  double c9Farads = 100e-12;    // Across R9; rolls the top of the gain back off.
  double r7Ohms = 47.0;         // n1 -> n6.
  double c7Farads = 2.2e-6;     // n6 -> ground. With R7, opens at 1.54 kHz.
  double r8Ohms = 560.0;        // n1 -> n7.
  double c8Farads = 4.7e-6;     // n7 -> ground. With R8, opens at 60 Hz.

  // --- LM308 -------------------------------------------------------------
  // The slew rate is not a defect to be modelled around; it is most of why a
  // RAT sounds like a RAT. At 0.3 V/us the part cannot follow a fast transient
  // at anything near full output, so hard attacks round off before the diodes
  // ever see them.
  //
  // These two numbers are not independent in the model: a transconductance
  // input stage driving the compensation capacitor gives a small-signal
  // unity-gain frequency of gbwHz and a large-signal limit of slewVoltsPerSec,
  // with the soft transition between them coming out of the differential pair
  // rather than being imposed.
  double slewVoltsPerSec = 0.3e6;
  double gbwHz = 1.0e6;
  // Output swing either side of the bias rail. An LM308 on a single 9 V supply
  // does not reach either rail.
  double outputSwingVolts = 3.5;

  // --- Clipping ----------------------------------------------------------
  double c10Farads = 4.7e-6;    // Vfb -> n8.
  double r10Ohms = 1000.0;      // n8 -> Vclip.
  // 1N914, standard SPICE parameters. The pair is matched and antiparallel, so
  // the static curve is symmetric; any asymmetry in the sound comes from the
  // op-amp above, not from here.
  double diodeSaturationCurrent = 2.52e-9;
  double diodeEmissionCoefficient = 1.752;
  double diodeThermalVolts = 0.02585;

  // --- Filter ------------------------------------------------------------
  // R17 is in series rather than being a divider, so turning it up moves the
  // corner down: 32 kHz at the bright end, 475 Hz at the dark end.
  double r17Ohms = 100000.0;    // Filter pot, Vclip -> n11.
  double r15Ohms = 1500.0;      // n11 -> Vtone.
  double c11Farads = 3.3e-9;    // Vtone -> ground.

  // --- Output ------------------------------------------------------------
  // J1 is a self-biased source follower, modelled as LINEAR. By this point the
  // signal has been clipped to well under a volt and filtered, and a follower
  // at that level is straight. Same call as the wah's input Darlington.
  double c12Farads = 22e-9;     // Vtone -> n9, the gate. 7.2 Hz with R12.
  double r12Ohms = 1e6;         // n9 -> ground.
  double c13Farads = 1e-6;      // n10 -> Vout. 1.6 Hz with R14.
  double r14Ohms = 100000.0;    // Volume pot.

  // Audio-taper approximation exponent, shared by all three pots: the wiper
  // resistance is the full track times position raised to this power.
  double taperExponent = 2.4;
};

const RatNetlist& ratNetlist();

// Rejects netlists that are not physically realizable. Every component value
// must be positive and finite.
bool ratNetlistValid(const RatNetlist& netlist);

// Maps a knob (0..1) to the wiper resistance of an audio-taper track.
double ratPotOhms(const RatNetlist& netlist, double trackOhms, double position);

} // namespace ardor
