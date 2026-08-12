// DK-method derivation for the GCB-95. Topology and component values come from
// docs/wah-gcb95-netlist.md; read that before changing anything here.
//
// The linear algebra is hand-rolled on purpose. The largest system is 19x19,
// solved once per pot position offline, so a third-party matrix library would
// be a new build input bought for nothing.
#include "wah/WahDk.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ardor {
namespace {

// --- Tiny dense matrix helpers -----------------------------------------

struct Mat {
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::vector<double> v;

  Mat() = default;
  Mat(std::size_t r, std::size_t c) : rows(r), cols(c), v(r * c, 0.0) {}
  double& at(std::size_t r, std::size_t c) { return v[r * cols + c]; }
  double at(std::size_t r, std::size_t c) const { return v[r * cols + c]; }
};

Mat multiply(const Mat& a, const Mat& b)
{
  Mat out(a.rows, b.cols);
  for (std::size_t i = 0; i < a.rows; ++i) {
    for (std::size_t k = 0; k < a.cols; ++k) {
      const double aik = a.at(i, k);
      if (aik == 0.0) continue;
      for (std::size_t j = 0; j < b.cols; ++j) {
        out.at(i, j) += aik * b.at(k, j);
      }
    }
  }
  return out;
}

// Solves A X = B by LU with partial pivoting. A is copied and destroyed.
Mat solve(Mat a, Mat b)
{
  const std::size_t n = a.rows;
  if (a.cols != n || b.rows != n) throw std::invalid_argument("solve: shape mismatch");

  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    double best = std::fabs(a.at(col, col));
    for (std::size_t r = col + 1; r < n; ++r) {
      const double candidate = std::fabs(a.at(r, col));
      if (candidate > best) {
        best = candidate;
        pivot = r;
      }
    }
    if (best < 1e-300) throw std::runtime_error("solve: singular matrix");
    if (pivot != col) {
      for (std::size_t j = 0; j < n; ++j) std::swap(a.at(col, j), a.at(pivot, j));
      for (std::size_t j = 0; j < b.cols; ++j) std::swap(b.at(col, j), b.at(pivot, j));
    }
    const double diagonal = a.at(col, col);
    for (std::size_t r = col + 1; r < n; ++r) {
      const double factor = a.at(r, col) / diagonal;
      if (factor == 0.0) continue;
      for (std::size_t j = col; j < n; ++j) a.at(r, j) -= factor * a.at(col, j);
      for (std::size_t j = 0; j < b.cols; ++j) b.at(r, j) -= factor * b.at(col, j);
    }
  }
  Mat x(n, b.cols);
  for (std::size_t j = 0; j < b.cols; ++j) {
    for (std::size_t i = n; i-- > 0;) {
      double sum = b.at(i, j);
      for (std::size_t c = i + 1; c < n; ++c) sum -= a.at(i, c) * x.at(c, j);
      x.at(i, j) = sum / a.at(i, i);
    }
  }
  return x;
}

// --- Circuit description ------------------------------------------------
//
// Node indices. Ground is kGnd and carries no equation.
enum Node : int {
  kVin = 0, kN3, kN7, kN2, kN1, kN8, kN9, kN5, kN6,
  kVout, kVfbIn, kN10, kN11, kVfb, kV9P0, kN4, kNL,
  kNodeCount,
};
constexpr int kGnd = -1;

constexpr std::size_t kStates = 8;       // 7 caps + 1 inductor.
constexpr std::size_t kPorts = 2;        // Q2 and Q3 base-emitter junctions.
constexpr std::size_t kInputs = 2;       // Audio sample, 9 V rail.
constexpr std::size_t kSources = 2;      // V2 (audio), V1 (supply).
constexpr std::size_t kAugmented = static_cast<std::size_t>(kNodeCount) + kSources;

struct Resistor {
  int a;
  int b;
  double ohms;
};

struct Reactive {
  int a;
  int b;
  double value;
  bool inductor;
};

struct Bjt {
  int base;
  int collector;
  int emitter;
  double beta;
};

std::vector<Resistor> resistors(const WahNetlist& n, double wiperOhms)
{
  // The pot is a divider across the 100k track: R12 is the grounded leg at
  // `wiperOhms`, R14 is the remainder. Both legs must stay positive or the
  // matrix goes singular at the sweep endpoints.
  const double r12 = std::max(wiperOhms, 1.0);
  const double r14 = std::max(n.potOhms - wiperOhms, 1.0);
  return {
    {kN3, kVin, n.r2Ohms},
    {kVin, kGnd, n.r3Ohms},
    {kN3, kV9P0, n.r1Ohms},
    {kGnd, kN7, n.r4Ohms},
    {kN7, kN2, n.r5Ohms},
    {kN5, kN1, n.r10Ohms},
    {kN8, kV9P0, n.r6Ohms},
    {kGnd, kN9, n.r7Ohms},
    {kN8, kN6, n.r8Ohms},
    {kN6, kN5, n.r9Ohms},
    {kN6, kGnd, n.r16Ohms},
    {kVfbIn, kVout, r14},
    {kGnd, kVfbIn, r12},
    {kN10, kN8, n.r11Ohms},
    {kN11, kV9P0, n.r18Ohms},
    {kGnd, kVfb, n.r19Ohms},
    {kNL, kN5, n.inductorSeriesOhms},
  };
}

std::vector<Reactive> reactives(const WahNetlist& n)
{
  return {
    {kVin, kN4, n.c3Farads, false},
    {kVin, kGnd, n.c6Farads, false},
    {kN1, kN2, n.c2Farads, false},
    {kN6, kGnd, n.c11Farads, false},
    {kVout, kN8, n.c1Farads, false},
    {kN10, kVfbIn, n.c8Farads, false},
    {kVfb, kN5, n.c7Farads, false},
    {kN6, kNL, n.inductorHenries, true},
  };
}

// Q2 and Q3 are the nonlinear ports, in that order. Q1 is linearized.
std::vector<Bjt> nonlinearBjts(const WahNetlist& n)
{
  return {
    {kN1, kN8, kN9, n.bjtBeta},
    {kN10, kN11, kVfb, n.bjtBeta},
  };
}

void stampResistor(Mat& s, int a, int b, double ohms)
{
  const double g = 1.0 / ohms;
  if (a >= 0) s.at(a, a) += g;
  if (b >= 0) s.at(b, b) += g;
  if (a >= 0 && b >= 0) {
    s.at(a, b) -= g;
    s.at(b, a) -= g;
  }
}

// Voltage-controlled current source: gm * (v[cp] - v[cn]) flows out of node
// `from` and into node `to`.
void stampVccs(Mat& s, int from, int to, int cp, int cn, double gm)
{
  if (from >= 0 && cp >= 0) s.at(from, cp) += gm;
  if (from >= 0 && cn >= 0) s.at(from, cn) -= gm;
  if (to >= 0 && cp >= 0) s.at(to, cp) -= gm;
  if (to >= 0 && cn >= 0) s.at(to, cn) += gm;
}

double diodeCurrent(const WahNetlist& n, double vbe)
{
  // Clamping the exponent argument matters: without it the first Newton step
  // from a cold start overflows to infinity and never recovers.
  const double vt = n.bjtEmissionCoefficient * n.bjtThermalVolts;
  const double arg = std::min(vbe / vt, 40.0);
  return n.bjtSaturationCurrent * (std::exp(arg) - 1.0);
}

double diodeConductance(const WahNetlist& n, double vbe)
{
  const double vt = n.bjtEmissionCoefficient * n.bjtThermalVolts;
  const double arg = std::min(vbe / vt, 40.0);
  return (n.bjtSaturationCurrent / vt) * std::exp(arg);
}

double nodeVoltage(const std::vector<double>& v, int node)
{
  return node < 0 ? 0.0 : v[static_cast<std::size_t>(node)];
}

// --- DC operating point -------------------------------------------------
//
// Capacitors open, the inductor a short, all three transistors nonlinear.
// Solved by damped Newton; the result linearizes Q1 and seeds the port
// conductances.
std::vector<double> solveDcOperatingPoint(const WahNetlist& n, double wiperOhms)
{
  const std::vector<Bjt> all{
    {kVin, kN3, kN7, n.q1Beta},
    {kN1, kN8, kN9, n.bjtBeta},
    {kN10, kN11, kVfb, n.bjtBeta},
  };
  const auto res = resistors(n, wiperOhms);

  std::vector<double> v(kAugmented, 0.0);
  // A sane starting guess: rail at the supply, everything else near a
  // conducting junction. Starting from all zeros makes the first exponential
  // step wildly overshoot.
  v[kV9P0] = n.supplyVolts;

  for (int iteration = 0; iteration < 200; ++iteration) {
    Mat s(kAugmented, kAugmented);
    Mat rhs(kAugmented, 1);

    for (const auto& r : res) stampResistor(s, r.a, r.b, r.ohms);
    // The inductor is a DC short; a milliohm link is short next to the 40 ohm
    // winding resistance and keeps the matrix better conditioned than an
    // ideal one would.
    stampResistor(s, kN6, kNL, 1e-3);
    // Capacitors are open at DC, so they contribute nothing.

    // Voltage sources: V2 (audio, zero at DC) at n4, V1 (supply) at V9P0.
    const std::size_t v2Row = static_cast<std::size_t>(kNodeCount);
    const std::size_t v1Row = v2Row + 1;
    s.at(v2Row, kN4) = 1.0;
    s.at(kN4, v2Row) = 1.0;
    rhs.at(v2Row, 0) = 0.0;
    s.at(v1Row, kV9P0) = 1.0;
    s.at(kV9P0, v1Row) = 1.0;
    rhs.at(v1Row, 0) = n.supplyVolts;

    // Nonlinear devices, linearized about the current iterate.
    for (const auto& q : all) {
      const double vbe = nodeVoltage(v, q.base) - nodeVoltage(v, q.emitter);
      const double ib = diodeCurrent(n, vbe);
      const double gpi = diodeConductance(n, vbe);
      const double gm = q.beta * gpi;

      // Base current: conductance gpi across base-emitter, with the companion
      // source removing the linearization offset.
      stampResistor(s, q.base, q.emitter, 1.0 / std::max(gpi, 1e-18));
      const double ibOffset = ib - gpi * vbe;
      if (q.base >= 0) rhs.at(q.base, 0) -= ibOffset;
      if (q.emitter >= 0) rhs.at(q.emitter, 0) += ibOffset;

      // Collector current: beta * ib, as a VCCS from collector to emitter.
      stampVccs(s, q.collector, q.emitter, q.base, q.emitter, gm);
      const double icOffset = q.beta * ib - gm * vbe;
      if (q.collector >= 0) rhs.at(q.collector, 0) -= icOffset;
      if (q.emitter >= 0) rhs.at(q.emitter, 0) += icOffset;
    }

    const Mat next = solve(s, rhs);
    double worst = 0.0;
    for (std::size_t i = 0; i < kAugmented; ++i) {
      // Damping the step is load-bearing: undamped Newton on a diode
      // exponential oscillates instead of converging.
      const double target = next.at(i, 0);
      const double delta = target - v[i];
      const double limited = std::clamp(delta, -0.5, 0.5);
      v[i] += limited;
      worst = std::max(worst, std::fabs(delta));
    }
    if (worst < 1e-10) break;
  }
  return v;
}

} // namespace

WahDkMatrices deriveWahDk(const WahNetlist& n, double wiperOhms, double sampleRate)
{
  const auto dc = solveDcOperatingPoint(n, wiperOhms);
  const auto res = resistors(n, wiperOhms);
  const auto react = reactives(n);
  const auto ports = nonlinearBjts(n);

  // --- Assemble the bordered MNA system --------------------------------
  Mat s(kAugmented, kAugmented);
  for (const auto& r : res) stampResistor(s, r.a, r.b, r.ohms);

  // Reactive companion conductances. gx = 2 C fs for capacitors,
  // 1/(2 L fs) for inductors; z = -1 for capacitors, +1 for inductors.
  std::vector<double> gx(kStates, 0.0);
  std::vector<double> z(kStates, 0.0);
  Mat nx(kStates, kAugmented);
  for (std::size_t i = 0; i < react.size(); ++i) {
    const auto& element = react[i];
    if (element.inductor) {
      gx[i] = 1.0 / (2.0 * element.value * sampleRate);
      z[i] = 1.0;
    } else {
      gx[i] = 2.0 * element.value * sampleRate;
      z[i] = -1.0;
    }
    stampResistor(s, element.a, element.b, 1.0 / gx[i]);
    if (element.a >= 0) nx.at(i, static_cast<std::size_t>(element.a)) += 1.0;
    if (element.b >= 0) nx.at(i, static_cast<std::size_t>(element.b)) -= 1.0;
  }

  // Q1, linearized about the DC point.
  {
    const double vbe = nodeVoltage(dc, kVin) - nodeVoltage(dc, kN7);
    const double gpi = diodeConductance(n, vbe);
    stampResistor(s, kVin, kN7, 1.0 / std::max(gpi, 1e-18));
    stampVccs(s, kN3, kN7, kVin, kN7, n.q1Beta * gpi);
  }

  // Voltage sources.
  const std::size_t v2Row = static_cast<std::size_t>(kNodeCount);
  const std::size_t v1Row = v2Row + 1;
  s.at(v2Row, kN4) = 1.0;
  s.at(kN4, v2Row) = 1.0;
  s.at(v1Row, kV9P0) = 1.0;
  s.at(kV9P0, v1Row) = 1.0;

  // --- Port incidence ---------------------------------------------------
  //
  // Voltage sensing differs from current injection. The port variable is the
  // transistor's base current; the collector draws beta times it and the
  // emitter returns (1 + beta) times it.
  Mat nnVoltage(kPorts, kAugmented);
  Mat nnCurrent(kPorts, kAugmented);
  for (std::size_t i = 0; i < ports.size(); ++i) {
    const auto& q = ports[i];
    nnVoltage.at(i, static_cast<std::size_t>(q.base)) += 1.0;
    if (q.emitter >= 0) nnVoltage.at(i, static_cast<std::size_t>(q.emitter)) -= 1.0;

    nnCurrent.at(i, static_cast<std::size_t>(q.base)) += 1.0;
    if (q.collector >= 0) nnCurrent.at(i, static_cast<std::size_t>(q.collector)) += q.beta;
    if (q.emitter >= 0) nnCurrent.at(i, static_cast<std::size_t>(q.emitter)) -= (1.0 + q.beta);
  }

  Mat no(1, kAugmented);
  no.at(0, kVout) = 1.0;

  // --- Right-hand-side injections ---------------------------------------
  Mat px(kAugmented, kStates);
  for (std::size_t i = 0; i < kStates; ++i) {
    for (std::size_t j = 0; j < kAugmented; ++j) px.at(j, i) = -nx.at(i, j) * z[i];
  }
  Mat pn(kAugmented, kPorts);
  for (std::size_t i = 0; i < kPorts; ++i) {
    for (std::size_t j = 0; j < kAugmented; ++j) pn.at(j, i) = -nnCurrent.at(i, j);
  }
  Mat pu(kAugmented, kInputs);
  pu.at(v2Row, 0) = 1.0; // Audio.
  pu.at(v1Row, 1) = 1.0; // Supply rail.

  // --- Solve once, reuse for every block --------------------------------
  const Mat sx = solve(s, px);
  const Mat sn = solve(s, pn);
  const Mat su = solve(s, pu);

  const Mat nxSx = multiply(nx, sx);
  const Mat nxSn = multiply(nx, sn);
  const Mat nxSu = multiply(nx, su);

  WahDkMatrices out;
  out.states = kStates;
  out.nonlinearPorts = kPorts;
  out.inputs = kInputs;
  out.a.assign(kStates * kStates, 0.0);
  out.b.assign(kStates * kInputs, 0.0);
  out.c.assign(kStates * kPorts, 0.0);

  for (std::size_t i = 0; i < kStates; ++i) {
    for (std::size_t j = 0; j < kStates; ++j) {
      out.a[i * kStates + j] = 2.0 * gx[i] * nxSx.at(i, j) + (i == j ? z[i] : 0.0);
    }
    for (std::size_t j = 0; j < kInputs; ++j) out.b[i * kInputs + j] = 2.0 * gx[i] * nxSu.at(i, j);
    for (std::size_t j = 0; j < kPorts; ++j) out.c[i * kPorts + j] = 2.0 * gx[i] * nxSn.at(i, j);
  }

  const Mat dMat = multiply(nnVoltage, sx);
  const Mat eMat = multiply(nnVoltage, su);
  const Mat fMat = multiply(nnVoltage, sn);
  out.d.assign(dMat.v.begin(), dMat.v.end());
  out.e.assign(eMat.v.begin(), eMat.v.end());
  out.f.assign(fMat.v.begin(), fMat.v.end());

  const Mat gMat = multiply(no, sx);
  const Mat hMat = multiply(no, su);
  const Mat kMat = multiply(no, sn);
  out.g.assign(gMat.v.begin(), gMat.v.end());
  out.h.assign(hMat.v.begin(), hMat.v.end());
  out.k.assign(kMat.v.begin(), kMat.v.end());

  out.portConductance.resize(kPorts);
  for (std::size_t i = 0; i < ports.size(); ++i) {
    const double vbe = nodeVoltage(dc, ports[i].base) - nodeVoltage(dc, ports[i].emitter);
    out.portConductance[i] = diodeConductance(n, vbe);
  }
  return out;
}

namespace {

// Folds the small-signal port conductances into the state space, giving the
// closed-loop linear system used by both analysis helpers.
struct LinearSystem {
  Mat a;
  Mat b; // Audio input column only.
  Mat c;
  double d = 0.0;
};

LinearSystem linearize(const WahDkMatrices& m)
{
  const std::size_t ns = m.states;
  const std::size_t np = m.nonlinearPorts;

  Mat fj(np, np);
  for (std::size_t i = 0; i < np; ++i) {
    for (std::size_t j = 0; j < np; ++j) {
      fj.at(i, j) = (i == j ? 1.0 : 0.0) - m.f[i * np + j] * m.portConductance[j];
    }
  }
  Mat dMat(np, ns);
  for (std::size_t i = 0; i < np; ++i) {
    for (std::size_t j = 0; j < ns; ++j) dMat.at(i, j) = m.d[i * ns + j];
  }
  Mat eMat(np, 1);
  for (std::size_t i = 0; i < np; ++i) eMat.at(i, 0) = m.e[i * m.inputs + 0];

  // v = (I - F J)^-1 (D x + E u)
  const Mat vx = solve(fj, dMat);
  const Mat vu = solve(fj, eMat);

  LinearSystem out;
  out.a = Mat(ns, ns);
  out.b = Mat(ns, 1);
  out.c = Mat(1, ns);
  for (std::size_t i = 0; i < ns; ++i) {
    for (std::size_t j = 0; j < ns; ++j) {
      double sum = m.a[i * ns + j];
      for (std::size_t p = 0; p < np; ++p) {
        sum += m.c[i * np + p] * m.portConductance[p] * vx.at(p, j);
      }
      out.a.at(i, j) = sum;
    }
    double sum = m.b[i * m.inputs + 0];
    for (std::size_t p = 0; p < np; ++p) {
      sum += m.c[i * np + p] * m.portConductance[p] * vu.at(p, 0);
    }
    out.b.at(i, 0) = sum;
  }
  for (std::size_t j = 0; j < ns; ++j) {
    double sum = m.g[j];
    for (std::size_t p = 0; p < np; ++p) {
      sum += m.k[p] * m.portConductance[p] * vx.at(p, j);
    }
    out.c.at(0, j) = sum;
  }
  out.d = m.h[0];
  for (std::size_t p = 0; p < np; ++p) {
    out.d += m.k[p] * m.portConductance[p] * vu.at(p, 0);
  }
  return out;
}

} // namespace

double wahSpectralRadius(const WahDkMatrices& matrices)
{
  const LinearSystem sys = linearize(matrices);
  const std::size_t n = sys.a.rows;
  std::vector<double> vec(n, 1.0);

  // Repeated squaring, via Gelfand's formula rho = lim ||A^k||^(1/k).
  //
  // Plain power iteration is wrong here: a resonator's dominant eigenvalues
  // are a complex-conjugate pair, so the iterate rotates and the per-step norm
  // oscillates instead of converging — which reads as spurious instability
  // right where Q is highest. Squaring reaches k = 2^24 in 24 multiplies and
  // gives a figure tight enough to tell genuine marginal instability from
  // measurement noise, which matters because this circuit legitimately sits
  // close to self-oscillation.
  (void)vec;
  constexpr int kSquarings = 24;
  Mat m = sys.a;
  double logScale = 0.0;
  for (int step = 0; step < kSquarings; ++step) {
    m = multiply(m, m);
    double biggest = 0.0;
    for (const double value : m.v) biggest = std::max(biggest, std::fabs(value));
    if (!std::isfinite(biggest)) return biggest;
    if (biggest < 1e-300) return 0.0;
    for (double& value : m.v) value /= biggest;
    logScale = 2.0 * logScale + std::log(biggest);
  }
  double residual = 0.0;
  for (const double value : m.v) residual = std::max(residual, std::fabs(value));
  const double exponent = std::ldexp(1.0, -kSquarings); // 2^-kSquarings
  return std::exp((logScale + std::log(std::max(residual, 1e-300))) * exponent);
}

double wahLinearMagnitude(const WahDkMatrices& matrices, double sampleRate, double frequencyHz)
{
  using Complex = std::complex<double>;
  const LinearSystem sys = linearize(matrices);
  const std::size_t n = sys.a.rows;
  const Complex zValue = std::polar(1.0, 2.0 * M_PI * frequencyHz / sampleRate);

  // (z I - A) w = B, then y = C w + D.
  std::vector<Complex> m(n * n);
  std::vector<Complex> rhs(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      m[i * n + j] = (i == j ? zValue : Complex(0.0)) - Complex(sys.a.at(i, j));
    }
    rhs[i] = Complex(sys.b.at(i, 0));
  }
  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    double best = std::abs(m[col * n + col]);
    for (std::size_t r = col + 1; r < n; ++r) {
      const double candidate = std::abs(m[r * n + col]);
      if (candidate > best) {
        best = candidate;
        pivot = r;
      }
    }
    if (best < 1e-300) return 0.0;
    if (pivot != col) {
      for (std::size_t j = 0; j < n; ++j) std::swap(m[col * n + j], m[pivot * n + j]);
      std::swap(rhs[col], rhs[pivot]);
    }
    for (std::size_t r = col + 1; r < n; ++r) {
      const Complex factor = m[r * n + col] / m[col * n + col];
      for (std::size_t j = col; j < n; ++j) m[r * n + j] -= factor * m[col * n + j];
      rhs[r] -= factor * rhs[col];
    }
  }
  std::vector<Complex> w(n);
  for (std::size_t i = n; i-- > 0;) {
    Complex sum = rhs[i];
    for (std::size_t c = i + 1; c < n; ++c) sum -= m[i * n + c] * w[c];
    w[i] = sum / m[i * n + i];
  }
  Complex y(sys.d);
  for (std::size_t i = 0; i < n; ++i) y += Complex(sys.c.at(0, i)) * w[i];
  return std::abs(y);
}

double wahLinearPeakHz(const WahDkMatrices& matrices, double sampleRate)
{
  constexpr int kPoints = 2048;
  const double lowHz = 50.0;
  const double highHz = std::min(6000.0, sampleRate * 0.45);
  double bestHz = lowHz;
  double best = -1.0;
  for (int i = 0; i < kPoints; ++i) {
    const double t = static_cast<double>(i) / (kPoints - 1);
    const double hz = lowHz * std::pow(highHz / lowHz, t);
    const double magnitude = wahLinearMagnitude(matrices, sampleRate, hz);
    if (std::isfinite(magnitude) && magnitude > best) {
      best = magnitude;
      bestHz = hz;
    }
  }
  return bestHz;
}

} // namespace ardor
