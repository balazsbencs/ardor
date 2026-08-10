#include "wah/WahDk.h"
#include "wah/WahNetlist.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool allFinite(const std::vector<double>& values)
{
  for (const double value : values) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

} // namespace

int main()
{
  const auto& netlist = ardor::gcb95Netlist();
  constexpr double kSampleRate = 192000.0; // 4x oversampled rate.

  const auto middle = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 0.5), kSampleRate);
  require(middle.states == 8, "the GCB-95 model should have 8 reactive states (7 caps + L1)");
  require(middle.nonlinearPorts == 2, "Q2 and Q3 should be the nonlinear ports");
  require(middle.inputs == 2, "the audio sample and the 9 V rail are both inputs");
  require(middle.a.size() == 64 && middle.b.size() == 16 && middle.c.size() == 16,
          "state matrices should be sized states x {states, inputs, ports}");
  require(middle.d.size() == 16 && middle.e.size() == 4 && middle.f.size() == 4,
          "port matrices should be sized ports x {states, inputs, ports}");
  require(middle.g.size() == 8 && middle.h.size() == 2 && middle.k.size() == 2,
          "output matrices should be sized 1 x {states, inputs, ports}");
  require(allFinite(middle.a) && allFinite(middle.b) && allFinite(middle.c)
            && allFinite(middle.d) && allFinite(middle.e) && allFinite(middle.f)
            && allFinite(middle.g) && allFinite(middle.h) && allFinite(middle.k),
          "no derived matrix entry should be non-finite");

  // The DC solve must actually bias the transistors on. A dead operating point
  // gives a model that is stable, finite, and silent.
  for (const double conductance : middle.portConductance) {
    require(std::isfinite(conductance) && conductance > 1e-9,
            "each nonlinear port should be biased into conduction at DC");
  }

  // Discrete-time stability: the closed-loop state matrix must have spectral
  // radius below 1, or the filter runs away regardless of what drives it.
  //
  // Expect roughly 0.9999999999, not a comfortable 0.99. The coupling caps
  // integrate charge, which puts a legitimate DC mode very close to z = 1, and
  // the circuit itself sits near self-oscillation at high Q. That narrow
  // margin is why wahSpectralRadius uses repeated squaring: plain power
  // iteration rotates on the dominant complex pair and reports noise of order
  // 1e-3, which is far too coarse to tell this apart from real instability.
  const double radius = ardor::wahSpectralRadius(middle);
  require(radius < 1.0, "the discretized state matrix must be stable");
  require(radius > 0.9, "a radius this far from 1 would mean the resonance had been damped away");

  // The whole point of the circuit: the resonant peak sweeps upward with the
  // pot, monotonically, across the audible wah range.
  double previousHz = 0.0;
  for (int i = 0; i <= 10; ++i) {
    const double position = i / 10.0;
    const auto matrices =
      ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, position), kSampleRate);
    require(ardor::wahSpectralRadius(matrices) < 1.0,
            "the model must be stable at every pot position, not just midpoint");
    const double peakHz = ardor::wahLinearPeakHz(matrices, kSampleRate);
    std::printf("position %.1f -> peak %.1f Hz\n", position, peakHz);
    require(peakHz > previousHz, "the resonant peak should rise monotonically toward the toe");
    previousHz = peakHz;
  }

  const auto heel = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 0.0), kSampleRate);
  const auto toe = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 1.0), kSampleRate);
  const double heelHz = ardor::wahLinearPeakHz(heel, kSampleRate);
  const double toeHz = ardor::wahLinearPeakHz(toe, kSampleRate);
  std::printf("heel %.1f Hz, toe %.1f Hz\n", heelHz, toeHz);
  require(heelHz > 300.0 && heelHz < 700.0,
          "heel-down peak should land near 400 Hz for a GCB-95");
  require(toeHz > 1300.0 && toeHz < 2600.0,
          "toe-down peak should land near 2 kHz for a GCB-95");
  return 0;
}
