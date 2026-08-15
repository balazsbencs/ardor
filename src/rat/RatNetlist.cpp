#include "rat/RatNetlist.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ardor {

const RatNetlist& ratNetlist()
{
  static const RatNetlist netlist{};
  return netlist;
}

bool ratNetlistValid(const RatNetlist& n)
{
  const std::array<double, 24> values{
    n.c3Farads, n.r2Ohms, n.r1Ohms, n.c2Farads,
    n.r9Ohms, n.c9Farads, n.r7Ohms, n.c7Farads, n.r8Ohms, n.c8Farads,
    n.slewVoltsPerSec, n.gbwHz, n.outputSwingVolts,
    n.c10Farads, n.r10Ohms, n.diodeSaturationCurrent,
    n.diodeEmissionCoefficient, n.diodeThermalVolts,
    n.r17Ohms, n.r15Ohms, n.c11Farads,
    n.c12Farads, n.r12Ohms, n.c13Farads,
  };
  return std::all_of(values.begin(), values.end(),
                     [](double value) { return std::isfinite(value) && value > 0.0; })
    && std::isfinite(n.r14Ohms) && n.r14Ohms > 0.0
    && std::isfinite(n.taperExponent) && n.taperExponent > 0.0;
}

double ratPotOhms(const RatNetlist& netlist, double trackOhms, double position)
{
  const double clamped = std::clamp(position, 0.0, 1.0);
  return trackOhms * std::pow(clamped, netlist.taperExponent);
}

} // namespace ardor
