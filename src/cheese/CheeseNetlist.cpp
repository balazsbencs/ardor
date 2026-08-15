#include "cheese/CheeseNetlist.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ardor {

const CheeseNetlist& cheeseNetlist()
{
  static const CheeseNetlist netlist{};
  return netlist;
}

bool cheeseNetlistValid(const CheeseNetlist& n)
{
  const std::array<double, 32> values{
    n.c3Farads, n.r3Ohms, n.r4Ohms, n.r2Ohms, n.c4Farads,
    n.c5Farads, n.r5Ohms, n.r6Ohms, n.r8Ohms, n.r9Ohms, n.c7Farads, n.c6Farads, n.r7Ohms,
    n.fuzzTrackOhms, n.c9Farads,
    n.c8Farads, n.r12Ohms, n.diodeSaturationCurrent, n.diodeEmissionCoefficient,
    n.clipperJunctionCount,
    n.r22Ohms, n.r23Ohms, n.c14Farads, n.c15Farads, n.r24Ohms, n.r25Ohms, n.c16Farads,
    n.toneTrackOhms, n.c12Farads, n.r15Ohms, n.r16Ohms, n.volumeTrackOhms,
  };
  const std::array<double, 11> more{
    n.r17Ohms, n.c10Farads, n.r18Ohms, n.c11Farads, n.r19Ohms, n.c13Farads,
    n.bjtSaturationCurrent, n.bjtEmissionCoefficient, n.thermalVolts, n.bjtBeta,
    n.taperExponent,
  };
  const auto positive = [](double value) { return std::isfinite(value) && value > 0.0; };
  return std::all_of(values.begin(), values.end(), positive)
    && std::all_of(more.begin(), more.end(), positive)
    && std::isfinite(n.supplyVolts) && n.supplyVolts > 0.0;
}

} // namespace ardor
