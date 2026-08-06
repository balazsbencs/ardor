#include "wah/WahTable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace ardor {
namespace {

double diodeCurrent(const WahNetlist& n, double v)
{
  const double vt = n.bjtEmissionCoefficient * n.bjtThermalVolts;
  const double arg = std::min(v / vt, 40.0);
  return n.bjtSaturationCurrent * (std::exp(arg) - 1.0);
}

double diodeConductance(const WahNetlist& n, double v)
{
  const double vt = n.bjtEmissionCoefficient * n.bjtThermalVolts;
  const double arg = std::min(v / vt, 40.0);
  return (n.bjtSaturationCurrent / vt) * std::exp(arg);
}

} // namespace

std::size_t wahMatrixStride(const WahTableHeader& h)
{
  const std::size_t s = h.states;
  const std::size_t p = h.ports;
  const std::size_t u = h.inputs;
  return s * s + s * u + s * p   // A, B, C
       + p * s + p * u + p * p   // D, E, F
       + s + u + p;              // G, H, K
}

bool solveWahPort(const WahDkMatrices& m, const WahNetlist& n,
                  double p1, double p2, double& i1, double& i2, int maxIterations)
{
  // Start from a conducting junction rather than zero; the exponential makes a
  // cold start from 0 V take a wildly overshooting first step.
  double v1 = 0.65;
  double v2 = 0.65;
  const double f11 = m.f[0];
  const double f12 = m.f[1];
  const double f21 = m.f[2];
  const double f22 = m.f[3];

  for (int iteration = 0; iteration < maxIterations; ++iteration) {
    const double c1 = diodeCurrent(n, v1);
    const double c2 = diodeCurrent(n, v2);
    const double g1 = diodeConductance(n, v1);
    const double g2 = diodeConductance(n, v2);

    // g(v) = v - p - F i(v)
    const double r1 = v1 - p1 - (f11 * c1 + f12 * c2);
    const double r2 = v2 - p2 - (f21 * c1 + f22 * c2);
    if (std::fabs(r1) < 1e-12 && std::fabs(r2) < 1e-12) break;

    // J = I - F diag(g)
    const double j11 = 1.0 - f11 * g1;
    const double j12 = -f12 * g2;
    const double j21 = -f21 * g1;
    const double j22 = 1.0 - f22 * g2;
    const double det = j11 * j22 - j12 * j21;
    if (!std::isfinite(det) || std::fabs(det) < 1e-300) return false;

    double d1 = -(j22 * r1 - j12 * r2) / det;
    double d2 = -(-j21 * r1 + j11 * r2) / det;

    // Damping. Without it Newton on a diode exponential oscillates instead of
    // converging, and the table gets holes exactly where the signal is hot.
    d1 = std::clamp(d1, -0.1, 0.1);
    d2 = std::clamp(d2, -0.1, 0.1);
    v1 += d1;
    v2 += d2;
    if (!std::isfinite(v1) || !std::isfinite(v2)) return false;
  }

  i1 = diodeCurrent(n, v1);
  i2 = diodeCurrent(n, v2);
  return std::isfinite(i1) && std::isfinite(i2);
}

void measureWahPortRange(const WahNetlist& n, double sampleRate, double position,
                         double amplitude, int frames,
                         double& p1Min, double& p1Max, double& p2Min, double& p2Max)
{
  const auto m = deriveWahDk(n, wahPotWiperOhms(n, position), sampleRate);
  const std::size_t ns = m.states;
  std::vector<double> x(ns, 0.0);
  double i1 = 0.0;
  double i2 = 0.0;

  for (int frame = 0; frame < frames; ++frame) {
    const double u0 = amplitude * std::sin(2.0 * M_PI * 220.0 * frame / sampleRate);
    const double u1 = n.supplyVolts;

    double p1 = m.e[0 * m.inputs + 0] * u0 + m.e[0 * m.inputs + 1] * u1;
    double p2 = m.e[1 * m.inputs + 0] * u0 + m.e[1 * m.inputs + 1] * u1;
    for (std::size_t j = 0; j < ns; ++j) {
      p1 += m.d[0 * ns + j] * x[j];
      p2 += m.d[1 * ns + j] * x[j];
    }
    if (!std::isfinite(p1) || !std::isfinite(p2)) break;

    p1Min = std::min(p1Min, p1);
    p1Max = std::max(p1Max, p1);
    p2Min = std::min(p2Min, p2);
    p2Max = std::max(p2Max, p2);

    if (!solveWahPort(m, n, p1, p2, i1, i2, 200)) break;

    std::vector<double> next(ns, 0.0);
    for (std::size_t r = 0; r < ns; ++r) {
      double sum = m.b[r * m.inputs + 0] * u0 + m.b[r * m.inputs + 1] * u1;
      for (std::size_t c = 0; c < ns; ++c) sum += m.a[r * ns + c] * x[c];
      sum += m.c[r * m.nonlinearPorts + 0] * i1 + m.c[r * m.nonlinearPorts + 1] * i2;
      next[r] = sum;
    }
    x.swap(next);
  }
}

bool buildWahTable(const WahNetlist& n, double sampleRate,
                   std::uint32_t gridP, std::uint32_t potCount,
                   WahTable& out, std::string& error)
{
  if (gridP < 2 || potCount < 2) {
    error = "wah table needs at least a 2x2 grid and 2 pot positions";
    return false;
  }
  if (!wahNetlistValid(n)) {
    error = "wah netlist is not physically realizable";
    return false;
  }

  // Measure the excursion the model actually reaches, across the sweep and
  // across input levels from quiet to hotter than any guitar. Padding by 25%
  // leaves headroom for transients the sine never produces.
  double p1Min = 1e300;
  double p1Max = -1e300;
  double p2Min = 1e300;
  double p2Max = -1e300;
  for (int i = 0; i < 5; ++i) {
    const double position = i / 4.0;
    for (const double amplitude : {0.05, 0.5, 2.0}) {
      measureWahPortRange(n, sampleRate, position, amplitude, 8000, p1Min, p1Max, p2Min, p2Max);
    }
  }
  if (!(p1Min < p1Max) || !(p2Min < p2Max)) {
    error = "could not measure a usable port range";
    return false;
  }
  const double pad1 = 0.25 * (p1Max - p1Min);
  const double pad2 = 0.25 * (p2Max - p2Min);
  p1Min -= pad1;
  p1Max += pad1;
  p2Min -= pad2;
  p2Max += pad2;

  out.header = WahTableHeader{};
  out.header.gridP = gridP;
  out.header.gridPot = potCount;
  out.header.sampleRate = static_cast<float>(sampleRate);
  out.header.p1Min = static_cast<float>(p1Min);
  out.header.p1Max = static_cast<float>(p1Max);
  out.header.p2Min = static_cast<float>(p2Min);
  out.header.p2Max = static_cast<float>(p2Max);

  std::vector<WahDkMatrices> perPosition(potCount);
  for (std::uint32_t p = 0; p < potCount; ++p) {
    const double position = static_cast<double>(p) / (potCount - 1);
    perPosition[p] = deriveWahDk(n, wahPotWiperOhms(n, position), sampleRate);
  }
  out.header.states = static_cast<std::uint32_t>(perPosition[0].states);
  out.header.ports = static_cast<std::uint32_t>(perPosition[0].nonlinearPorts);
  out.header.inputs = static_cast<std::uint32_t>(perPosition[0].inputs);

  const std::size_t stride = wahMatrixStride(out.header);
  out.matrices.assign(static_cast<std::size_t>(potCount) * stride, 0.0f);
  for (std::uint32_t p = 0; p < potCount; ++p) {
    const auto& m = perPosition[p];
    std::size_t offset = static_cast<std::size_t>(p) * stride;
    const auto append = [&](const std::vector<double>& src) {
      for (const double value : src) out.matrices[offset++] = static_cast<float>(value);
    };
    append(m.a);
    append(m.b);
    append(m.c);
    append(m.d);
    append(m.e);
    append(m.f);
    append(m.g);
    append(m.h);
    append(m.k);
  }

  const std::size_t ports = out.header.ports;
  out.solutions.assign(
    static_cast<std::size_t>(potCount) * gridP * gridP * ports, 0.0f);
  for (std::uint32_t p = 0; p < potCount; ++p) {
    const auto& m = perPosition[p];
    for (std::uint32_t b = 0; b < gridP; ++b) {
      const double p2 = p2Min + (p2Max - p2Min) * b / (gridP - 1);
      for (std::uint32_t a = 0; a < gridP; ++a) {
        const double p1 = p1Min + (p1Max - p1Min) * a / (gridP - 1);
        double i1 = 0.0;
        double i2 = 0.0;
        if (!solveWahPort(m, n, p1, p2, i1, i2, 400)) {
          error = "Newton failed to converge at pot " + std::to_string(p)
            + " grid (" + std::to_string(a) + "," + std::to_string(b) + ")";
          return false;
        }
        const std::size_t index =
          (((static_cast<std::size_t>(p) * gridP + b) * gridP) + a) * ports;
        out.solutions[index + 0] = static_cast<float>(i1);
        out.solutions[index + 1] = static_cast<float>(i2);
      }
    }
  }
  return true;
}

bool writeWahTable(const std::filesystem::path& path, const WahTable& table, std::string& error)
{
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    error = "could not open " + path.string() + " for writing";
    return false;
  }
  file.write(reinterpret_cast<const char*>(&table.header), sizeof(WahTableHeader));
  file.write(reinterpret_cast<const char*>(table.matrices.data()),
             static_cast<std::streamsize>(table.matrices.size() * sizeof(float)));
  file.write(reinterpret_cast<const char*>(table.solutions.data()),
             static_cast<std::streamsize>(table.solutions.size() * sizeof(float)));
  if (!file) {
    error = "failed while writing " + path.string();
    return false;
  }
  return true;
}

bool readWahTable(const std::filesystem::path& path, WahTable& table, std::string& error)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "could not open " + path.string();
    return false;
  }
  const auto size = static_cast<std::uintmax_t>(file.tellg());
  file.seekg(0);
  if (size < sizeof(WahTableHeader)) {
    error = "wah table is shorter than its header";
    return false;
  }
  file.read(reinterpret_cast<char*>(&table.header), sizeof(WahTableHeader));
  const WahTableHeader reference{};
  if (table.header.magic != reference.magic) {
    error = "wah table has the wrong magic";
    return false;
  }
  if (table.header.version != reference.version) {
    error = "wah table version " + std::to_string(table.header.version) + " is unsupported";
    return false;
  }
  if (table.header.gridP < 2 || table.header.gridPot < 2
      || table.header.states == 0 || table.header.ports == 0 || table.header.inputs == 0) {
    error = "wah table header describes an empty model";
    return false;
  }

  const std::size_t matrixCount =
    static_cast<std::size_t>(table.header.gridPot) * wahMatrixStride(table.header);
  const std::size_t solutionCount =
    static_cast<std::size_t>(table.header.gridPot) * table.header.gridP
    * table.header.gridP * table.header.ports;
  const std::uintmax_t expected =
    sizeof(WahTableHeader) + (matrixCount + solutionCount) * sizeof(float);
  // An exact length check is what turns a truncated or mismatched file into a
  // clean load failure rather than silently interpreted garbage.
  if (size != expected) {
    error = "wah table length " + std::to_string(size) + " does not match the "
      + std::to_string(expected) + " its header implies";
    return false;
  }

  table.matrices.resize(matrixCount);
  table.solutions.resize(solutionCount);
  file.read(reinterpret_cast<char*>(table.matrices.data()),
            static_cast<std::streamsize>(matrixCount * sizeof(float)));
  file.read(reinterpret_cast<char*>(table.solutions.data()),
            static_cast<std::streamsize>(solutionCount * sizeof(float)));
  if (!file) {
    error = "failed while reading " + path.string();
    return false;
  }
  return true;
}

} // namespace ardor
