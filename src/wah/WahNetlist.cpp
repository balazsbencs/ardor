// Component values and topology extracted from the LTspice schematic at
//   https://github.com/Cushychicken/ltspice-guitar-pedals
//   dunlop-crybaby-wah/dunlop-crybaby-wah.asc (retrieved 2026-08-06)
//
// The connectivity was resolved from that file's wire graph rather than read
// off a picture; every symbol pin landed exactly on a wire endpoint, which is
// what makes the extraction self-checking. The full node-by-node netlist is
// recorded in docs/wah-gcb95-netlist.md.
//
// Component values cross-check against the ElectroSmash GCB-95 analysis
// (https://electrosmash.mas-effects.com/crybaby-gcb-95.html), which supplies
// the transistor part numbers but not usable connectivity.
//
// The circuit has three transistors: Q1 MPSA13 Darlington input buffer,
// Q2 MPSA18 active filter stage, Q3 MPSA18 feedback follower. Q1 is modelled
// as linear; see the comment on its fields in WahNetlist.h for why.
//
// Correctness against a physical pedal is confirmed by the manual
// response-curve check, not by any automated test in this repository.
#include "wah/WahNetlist.h"

#include <algorithm>
#include <cmath>

namespace ardor {
namespace {

bool positiveFinite(double value)
{
  return std::isfinite(value) && value > 0.0;
}

} // namespace

const WahNetlist& gcb95Netlist()
{
  static const WahNetlist netlist{};
  return netlist;
}

bool wahNetlistValid(const WahNetlist& netlist)
{
  return positiveFinite(netlist.c3Farads)
    && positiveFinite(netlist.r2Ohms)
    && positiveFinite(netlist.r3Ohms)
    && positiveFinite(netlist.c6Farads)
    && positiveFinite(netlist.r1Ohms)
    && positiveFinite(netlist.r4Ohms)
    && positiveFinite(netlist.q1Beta)
    && positiveFinite(netlist.r5Ohms)
    && positiveFinite(netlist.c2Farads)
    && positiveFinite(netlist.r10Ohms)
    && positiveFinite(netlist.r6Ohms)
    && positiveFinite(netlist.r7Ohms)
    && positiveFinite(netlist.inductorHenries)
    && positiveFinite(netlist.inductorSeriesOhms)
    && positiveFinite(netlist.r9Ohms)
    && positiveFinite(netlist.r8Ohms)
    && positiveFinite(netlist.c11Farads)
    && positiveFinite(netlist.r16Ohms)
    && positiveFinite(netlist.c1Farads)
    && positiveFinite(netlist.potOhms)
    && positiveFinite(netlist.c8Farads)
    && positiveFinite(netlist.r11Ohms)
    && positiveFinite(netlist.r18Ohms)
    && positiveFinite(netlist.r19Ohms)
    && positiveFinite(netlist.c7Farads)
    && positiveFinite(netlist.bjtSaturationCurrent)
    && positiveFinite(netlist.bjtEmissionCoefficient)
    && positiveFinite(netlist.bjtThermalVolts)
    && positiveFinite(netlist.bjtBeta)
    && positiveFinite(netlist.supplyVolts)
    && positiveFinite(netlist.taperExponent);
}

double wahPotWiperOhms(const WahNetlist& netlist, double position)
{
  // Returns the GROUNDED leg (R12), which DECREASES as the treadle goes down
  // toward the toe. Grounding more of the divider sends less of the output to
  // the feedback follower, and the resonance climbs.
  //
  // The direction is not a matter of taste: measured against the derived
  // model, R12 = 100k puts the peak at ~400 Hz and R12 = 0 puts it at
  // ~2.2 kHz. Heel-down must be the low end, so heel is the full-track end.
  const double clamped = std::clamp(position, 0.0, 1.0);
  return netlist.potOhms * std::pow(1.0 - clamped, netlist.taperExponent);
}

} // namespace ardor
