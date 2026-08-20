// DK-method derivation for the Lovetone Big Cheese. Topology and component
// values come from docs/big-cheese-netlist.md; read that before changing
// anything here, including the section on where the source is incomplete.
//
// The dense linear algebra and the nodal stamping live in circuit/MnaMatrix.h,
// shared with the wah derivation. What is here is this circuit and nothing else.
#include "cheese/CheeseDk.h"

#include "circuit/MnaMatrix.h"

#include <algorithm>
#include <cmath>

namespace ardor {
namespace {

using circuit::Mat;
using circuit::multiply;
using circuit::nodeVoltage;
using circuit::solve;
using circuit::stampResistor;
using circuit::stampVccs;

// Node indices. Ground is kGnd and carries no equation. kVsrc is the input
// buffer's output, which is an op-amp output and so an ideal voltage source.
enum Node : int {
  kVsrc = 0, kN5, kN8, kN10, kN9, kN7, kN11, kN12,
  kN13, kN14, kN24, kN25, kN26, kN21, kN17, kVa,
  kNodeCount,
};
constexpr int kGnd = -1;

constexpr std::size_t kStates = 8;   // C5, C6, C7, C9, C8, the tone cap, C16, C12.
constexpr std::size_t kPorts = 3;    // Q1 base-emitter, Q2 base-emitter, the clipper.
constexpr std::size_t kInputs = 2;   // Audio sample, supply rail.
constexpr std::size_t kSources = 2;  // The buffer output and the rail.
constexpr std::size_t kAugmented = static_cast<std::size_t>(kNodeCount) + kSources;

struct Resistor {
  int a;
  int b;
  double ohms;
};

struct Capacitor {
  int a;
  int b;
  double farads;
};

struct Bjt {
  int base;
  int collector;
  int emitter;
};

double taper(const CheeseNetlist& n, double track, double position)
{
  return track * std::pow(std::clamp(position, 0.0, 1.0), n.taperExponent);
}

std::vector<Resistor> resistors(const CheeseNetlist& n, const CheeseControls& controls)
{
  // Both legs of every pot must stay positive or the matrix goes singular at
  // the ends of the sweep.
  const double fuzzLower = std::clamp(taper(n, n.fuzzTrackOhms, controls.fuzz),
                                      1.0, n.fuzzTrackOhms - 1.0);
  const double fuzzUpper = n.fuzzTrackOhms - fuzzLower;
  // The tone pot is linear, not audio taper. A Big Muff stack is a pair of
  // filters meeting at the wiper, and where they meet is the control: bending
  // the track with a taper moves the scoop off the middle of the travel and
  // hides it.
  const double toneLower = std::clamp(n.toneTrackOhms * std::clamp(controls.tone, 0.0, 1.0),
                                      1.0, n.toneTrackOhms - 1.0);
  const double toneUpper = n.toneTrackOhms - toneLower;

  return {
    {kN5, kN7, n.r5Ohms},
    {kVa, kN8, n.r6Ohms},
    {kN10, kGnd, n.r8Ohms},
    {kN11, kN10, n.r9Ohms},
    {kVa, kN9, n.r7Ohms},
    {kN7, kN12, fuzzUpper},
    {kN12, kGnd, fuzzLower},
    {kN14, kN13, n.r12Ohms},
    {kN24, kN14, n.r22Ohms},
    {kGnd, kN24, n.r23Ohms},
    {kGnd, kN25, n.r24Ohms},
    {kN26, kN14, n.r25Ohms},
    {kN25, kN21, toneUpper},
    {kN21, kN26, toneLower},
    {kN17, kVa, n.r15Ohms},
    {kN17, kGnd, n.r16Ohms},
  };
}

std::vector<Capacitor> capacitors(const CheeseNetlist& n)
{
  // C14 and C15 sit across the same pair of nodes, so they are one capacitor.
  return {
    {kVsrc, kN5, n.c5Farads},
    {kN9, kN8, n.c6Farads},
    {kN9, kN11, n.c7Farads},
    {kN12, kGnd, n.c9Farads},
    {kN13, kN9, n.c8Farads},
    {kN14, kN25, n.c14Farads + n.c15Farads},
    {kN26, kGnd, n.c16Farads},
    {kN17, kN21, n.c12Farads},
  };
}

const std::vector<Bjt>& transistors()
{
  static const std::vector<Bjt> devices{
    {kN5, kN8, kN10},   // Q1
    {kN8, kN9, kN7},    // Q2
  };
  return devices;
}

double bjtCurrent(const CheeseNetlist& n, double vbe)
{
  return circuit::junctionCurrent(vbe, n.bjtSaturationCurrent,
                                  n.bjtEmissionCoefficient * n.thermalVolts);
}

double bjtConductance(const CheeseNetlist& n, double vbe)
{
  return circuit::junctionConductance(vbe, n.bjtSaturationCurrent,
                                      n.bjtEmissionCoefficient * n.thermalVolts);
}

// The clipping node. D2 conducts out of it; Q3's two paralleled junctions
// conduct into it. Current is positive flowing out of the node into ground,
// which is the port convention used below.
double clipperCurrent(const CheeseNetlist& n, double v)
{
  const double vt = n.diodeEmissionCoefficient * n.thermalVolts;
  return circuit::junctionCurrent(v, n.diodeSaturationCurrent, vt)
    - n.clipperJunctionCount * circuit::junctionCurrent(-v, n.diodeSaturationCurrent, vt);
}

double clipperConductance(const CheeseNetlist& n, double v)
{
  const double vt = n.diodeEmissionCoefficient * n.thermalVolts;
  return circuit::junctionConductance(v, n.diodeSaturationCurrent, vt)
    + n.clipperJunctionCount * circuit::junctionConductance(-v, n.diodeSaturationCurrent, vt);
}

} // namespace

// --- DC operating point ---------------------------------------------------
//
// Capacitors open, every nonlinear device in circuit, solved by damped Newton.
// The result seeds the runtime's port conductances and starting voltages.
std::vector<double> cheeseOperatingPoint(const CheeseNetlist& n, const CheeseControls& controls)
{
  const auto res = resistors(n, controls);

  std::vector<double> v(kAugmented, 0.0);
  // A sane starting guess. Starting from all zeros makes the first exponential
  // step wildly overshoot, the same way it does in the wah derivation.
  v[kVa] = n.supplyVolts;
  v[kN8] = n.supplyVolts * 0.5;
  v[kN9] = n.supplyVolts * 0.5;
  v[kN17] = n.supplyVolts * 0.5;

  for (int iteration = 0; iteration < 400; ++iteration) {
    Mat s(kAugmented, kAugmented);
    Mat rhs(kAugmented, 1);

    for (const auto& r : res) stampResistor(s, r.a, r.b, r.ohms);
    // Capacitors are open at DC, so they contribute nothing. Node n13 is left
    // hanging off R12 alone, which is not singular: it simply settles at the
    // clipping node's voltage.

    const std::size_t audioRow = static_cast<std::size_t>(kNodeCount);
    const std::size_t railRow = audioRow + 1;
    s.at(audioRow, kVsrc) = 1.0;
    s.at(kVsrc, audioRow) = 1.0;
    rhs.at(audioRow, 0) = 0.0;
    s.at(railRow, kVa) = 1.0;
    s.at(kVa, railRow) = 1.0;
    rhs.at(railRow, 0) = n.supplyVolts;

    for (const auto& q : transistors()) {
      const double vbe = nodeVoltage(v, q.base) - nodeVoltage(v, q.emitter);
      const double ib = bjtCurrent(n, vbe);
      const double gpi = bjtConductance(n, vbe);
      const double gm = n.bjtBeta * gpi;

      stampResistor(s, q.base, q.emitter, 1.0 / std::max(gpi, 1e-18));
      const double baseOffset = ib - gpi * vbe;
      if (q.base >= 0) rhs.at(q.base, 0) -= baseOffset;
      if (q.emitter >= 0) rhs.at(q.emitter, 0) += baseOffset;

      stampVccs(s, q.collector, q.emitter, q.base, q.emitter, gm);
      const double collectorOffset = n.bjtBeta * ib - gm * vbe;
      if (q.collector >= 0) rhs.at(q.collector, 0) -= collectorOffset;
      if (q.emitter >= 0) rhs.at(q.emitter, 0) += collectorOffset;
    }

    {
      const double vc = nodeVoltage(v, kN14);
      const double current = clipperCurrent(n, vc);
      const double conductance = clipperConductance(n, vc);
      stampResistor(s, kN14, kGnd, 1.0 / std::max(conductance, 1e-18));
      rhs.at(kN14, 0) -= current - conductance * vc;
    }

    const Mat next = solve(s, rhs);
    double worst = 0.0;
    for (std::size_t i = 0; i < kAugmented; ++i) {
      // Damping the step is load-bearing: undamped Newton on a diode
      // exponential oscillates instead of converging.
      const double delta = next.at(i, 0) - v[i];
      v[i] += std::clamp(delta, -0.5, 0.5);
      worst = std::max(worst, std::fabs(delta));
    }
    if (worst < 1e-10) break;
  }
  return v;
}

double cheeseQ1CollectorVolts(const std::vector<double>& op) { return nodeVoltage(op, kN8); }
double cheeseQ2CollectorVolts(const std::vector<double>& op) { return nodeVoltage(op, kN9); }

double cheeseQ1BaseEmitterVolts(const std::vector<double>& op)
{
  return nodeVoltage(op, kN5) - nodeVoltage(op, kN10);
}

double cheeseQ2BaseEmitterVolts(const std::vector<double>& op)
{
  return nodeVoltage(op, kN8) - nodeVoltage(op, kN7);
}

CheeseDkMatrices deriveCheeseDk(const CheeseNetlist& n, const CheeseControls& controls,
                                double sampleRate)
{
  const auto dc = cheeseOperatingPoint(n, controls);
  const auto res = resistors(n, controls);
  const auto caps = capacitors(n);
  const auto& ports = transistors();

  // --- Assemble the bordered MNA system ------------------------------------
  Mat s(kAugmented, kAugmented);
  for (const auto& r : res) stampResistor(s, r.a, r.b, r.ohms);

  // Trapezoidal companion conductances: gx = 2 C fs, and the state carries the
  // sign convention z = -1 for a capacitor.
  std::vector<double> gx(kStates, 0.0);
  Mat nx(kStates, kAugmented);
  for (std::size_t i = 0; i < caps.size(); ++i) {
    const auto& element = caps[i];
    gx[i] = 2.0 * element.farads * sampleRate;
    stampResistor(s, element.a, element.b, 1.0 / gx[i]);
    if (element.a >= 0) nx.at(i, static_cast<std::size_t>(element.a)) += 1.0;
    if (element.b >= 0) nx.at(i, static_cast<std::size_t>(element.b)) -= 1.0;
  }

  const std::size_t audioRow = static_cast<std::size_t>(kNodeCount);
  const std::size_t railRow = audioRow + 1;
  s.at(audioRow, kVsrc) = 1.0;
  s.at(kVsrc, audioRow) = 1.0;
  s.at(railRow, kVa) = 1.0;
  s.at(kVa, railRow) = 1.0;

  // --- Port incidence ------------------------------------------------------
  //
  // Voltage sensing differs from current injection. For a transistor the port
  // variable is the base current: the collector draws beta times it and the
  // emitter returns one plus beta times it. For the clipper the port variable
  // is just the current leaving the node.
  Mat nnVoltage(kPorts, kAugmented);
  Mat nnCurrent(kPorts, kAugmented);
  for (std::size_t i = 0; i < ports.size(); ++i) {
    const auto& q = ports[i];
    nnVoltage.at(i, static_cast<std::size_t>(q.base)) += 1.0;
    if (q.emitter >= 0) nnVoltage.at(i, static_cast<std::size_t>(q.emitter)) -= 1.0;

    nnCurrent.at(i, static_cast<std::size_t>(q.base)) += 1.0;
    if (q.collector >= 0) nnCurrent.at(i, static_cast<std::size_t>(q.collector)) += n.bjtBeta;
    if (q.emitter >= 0) nnCurrent.at(i, static_cast<std::size_t>(q.emitter)) -= (1.0 + n.bjtBeta);
  }
  nnVoltage.at(2, kN14) += 1.0;
  nnCurrent.at(2, kN14) += 1.0;

  Mat no(1, kAugmented);
  no.at(0, kN17) = 1.0;

  // --- Right-hand-side injections -----------------------------------------
  Mat px(kAugmented, kStates);
  for (std::size_t i = 0; i < kStates; ++i) {
    for (std::size_t j = 0; j < kAugmented; ++j) px.at(j, i) = nx.at(i, j);
  }
  Mat pn(kAugmented, kPorts);
  for (std::size_t i = 0; i < kPorts; ++i) {
    for (std::size_t j = 0; j < kAugmented; ++j) pn.at(j, i) = -nnCurrent.at(i, j);
  }
  Mat pu(kAugmented, kInputs);
  pu.at(audioRow, 0) = 1.0;
  pu.at(railRow, 1) = 1.0;

  // --- Solve once, reuse for every sample ----------------------------------
  const Mat sx = solve(s, px);
  const Mat sn = solve(s, pn);
  const Mat su = solve(s, pu);

  const Mat nxSx = multiply(nx, sx);
  const Mat nxSn = multiply(nx, sn);
  const Mat nxSu = multiply(nx, su);

  CheeseDkMatrices out;
  out.states = kStates;
  out.ports = kPorts;
  out.inputs = kInputs;
  out.a.assign(kStates * kStates, 0.0);
  out.b.assign(kStates * kInputs, 0.0);
  out.c.assign(kStates * kPorts, 0.0);

  for (std::size_t i = 0; i < kStates; ++i) {
    for (std::size_t j = 0; j < kStates; ++j) {
      out.a[i * kStates + j] = 2.0 * gx[i] * nxSx.at(i, j) - (i == j ? 1.0 : 0.0);
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

  out.portConductance = {
    bjtConductance(n, cheeseQ1BaseEmitterVolts(dc)),
    bjtConductance(n, cheeseQ2BaseEmitterVolts(dc)),
    clipperConductance(n, nodeVoltage(dc, kN14)),
  };
  out.portVoltage = {
    cheeseQ1BaseEmitterVolts(dc),
    cheeseQ2BaseEmitterVolts(dc),
    nodeVoltage(dc, kN14),
  };
  out.outputOffset = nodeVoltage(dc, kN17);
  return out;
}

} // namespace ardor
