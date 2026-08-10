#include "wah/WahDk.h"
#include "wah/WahNetlist.h"
#include "wah/WahTable.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  const auto& netlist = ardor::gcb95Netlist();
  constexpr double kSampleRate = 192000.0;
  const auto matrices =
    ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 0.5), kSampleRate);

  // Newton must converge across the whole span the grid will cover, including
  // the corners where the exponential is at its most hostile.
  {
    double p1Min = 1e300;
    double p1Max = -1e300;
    double p2Min = 1e300;
    double p2Max = -1e300;
    ardor::measureWahPortRange(netlist, kSampleRate, 0.5, 2.0, 4000,
                               p1Min, p1Max, p2Min, p2Max);
    require(p1Min < p1Max && p2Min < p2Max, "the measured port range should be non-empty");
    std::printf("p1 %.4f..%.4f  p2 %.4f..%.4f\n", p1Min, p1Max, p2Min, p2Max);

    for (int a = 0; a <= 4; ++a) {
      for (int b = 0; b <= 4; ++b) {
        const double p1 = p1Min + (p1Max - p1Min) * a / 4.0;
        const double p2 = p2Min + (p2Max - p2Min) * b / 4.0;
        double i1 = 0.0;
        double i2 = 0.0;
        require(ardor::solveWahPort(matrices, netlist, p1, p2, i1, i2, 400),
                "Newton should converge at grid corner (" + std::to_string(a) + ","
                  + std::to_string(b) + ")");
        require(std::isfinite(i1) && std::isfinite(i2), "solved currents should be finite");
      }
    }
  }

  // The solved currents must satisfy the circuit equation they came from.
  // Convergence alone does not prove the residual is small.
  {
    double i1 = 0.0;
    double i2 = 0.0;
    const double p1 = -1.0;
    const double p2 = -1.0;
    require(ardor::solveWahPort(matrices, netlist, p1, p2, i1, i2, 400), "solve should converge");
    // v = p + F i must reproduce currents consistent with the diode law.
    const double v1 = p1 + matrices.f[0] * i1 + matrices.f[1] * i2;
    const double v2 = p2 + matrices.f[2] * i1 + matrices.f[3] * i2;
    const double vt = netlist.bjtEmissionCoefficient * netlist.bjtThermalVolts;
    const double check1 =
      netlist.bjtSaturationCurrent * (std::exp(std::min(v1 / vt, 40.0)) - 1.0);
    const double check2 =
      netlist.bjtSaturationCurrent * (std::exp(std::min(v2 / vt, 40.0)) - 1.0);
    require(std::fabs(check1 - i1) < 1e-9 * std::max(1.0, std::fabs(i1)),
            "port 1 current should satisfy the diode law at the solved voltage");
    require(std::fabs(check2 - i2) < 1e-9 * std::max(1.0, std::fabs(i2)),
            "port 2 current should satisfy the diode law at the solved voltage");
  }

  // Round-trip the on-disk format at a small grid so the test stays fast.
  const auto path = std::filesystem::temp_directory_path() / "wah_table_smoke.wahtable";
  ardor::WahTable written;
  std::string error;
  require(ardor::buildWahTable(netlist, kSampleRate, 16, 5, written, error), error);
  require(written.header.states == 8, "the table should carry 8 states");
  require(written.header.ports == 2, "the table should carry 2 ports");
  require(written.solutions.size() == 5u * 16u * 16u * 2u, "solution count should match the grid");
  require(ardor::writeWahTable(path, written, error), error);

  ardor::WahTable read;
  require(ardor::readWahTable(path, read, error), error);
  require(read.header.gridP == written.header.gridP, "grid width should survive the round trip");
  require(read.header.gridPot == written.header.gridPot, "pot count should survive the round trip");
  require(read.solutions.size() == written.solutions.size(), "solution count should match");
  for (std::size_t i = 0; i < written.solutions.size(); ++i) {
    require(read.solutions[i] == written.solutions[i],
            "solutions should round-trip bit-exactly; the checked-in table depends on it");
  }
  for (std::size_t i = 0; i < written.matrices.size(); ++i) {
    require(read.matrices[i] == written.matrices[i], "matrices should round-trip bit-exactly");
  }

  // A truncated file must be rejected, not read as garbage.
  std::filesystem::resize_file(path, 8);
  ardor::WahTable truncated;
  require(!ardor::readWahTable(path, truncated, error),
          "a truncated table file should fail to load");
  require(!error.empty(), "a failed load should explain itself");

  ardor::WahTable missing;
  require(!ardor::readWahTable("does/not/exist.wahtable", missing, error),
          "a missing table file should fail to load");
  std::filesystem::remove(path);
  return 0;
}
