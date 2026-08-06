// Component values transcribed from the ElectroSmash Dunlop Crybaby GCB-95
// circuit analysis:
//   https://electrosmash.mas-effects.com/crybaby-gcb-95.html
//   (mirror of https://www.electrosmash.com/crybaby-gcb-95, retrieved
//   2026-08-06)
//
// That source lists three transistors: Q0 MPSA13 Darlington input buffer,
// Q1 MPSA18 filter/gain stage, Q2 MPSA18 output stage. Q0 is modelled as
// linear here; see the comment on its fields in WahNetlist.h for why.
//
// These values are the starting point for the DK derivation in WahDk.
// Correctness against the physical circuit is confirmed by the manual
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
  return positiveFinite(netlist.cin1Farads)
    && positiveFinite(netlist.r1Ohms)
    && positiveFinite(netlist.r5Ohms)
    && positiveFinite(netlist.r6Ohms)
    && positiveFinite(netlist.r9Ohms)
    && positiveFinite(netlist.q0Beta)
    && positiveFinite(netlist.r2Ohms)
    && positiveFinite(netlist.r3Ohms)
    && positiveFinite(netlist.r4Ohms)
    && positiveFinite(netlist.c3Farads)
    && positiveFinite(netlist.inductorHenries)
    && positiveFinite(netlist.inductorSeriesOhms)
    && positiveFinite(netlist.c1Farads)
    && positiveFinite(netlist.c2Farads)
    && positiveFinite(netlist.potOhms)
    && positiveFinite(netlist.r7Ohms)
    && positiveFinite(netlist.r8Ohms)
    && positiveFinite(netlist.r10Ohms)
    && positiveFinite(netlist.c4Farads)
    && positiveFinite(netlist.c5Farads)
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
  const double clamped = std::clamp(position, 0.0, 1.0);
  return netlist.potOhms * std::pow(clamped, netlist.taperExponent);
}

} // namespace ardor
