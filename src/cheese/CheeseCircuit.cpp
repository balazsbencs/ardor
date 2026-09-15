#include "cheese/CheeseCircuit.h"

#include "circuit/MnaMatrix.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ardor {

namespace {

constexpr double kTwoPi = 6.28318530718;

// The solve stops when it has converged, with a hard cap. A fixed count was
// tried at three, four and six: the spectrum of a held tone matches a converged
// reference at all of them, but a decaying note still leaves isolated samples,
// scattered through the decay rather than bunched at the onset, where the solve
// had not finished and the output is out by a third of the peak. Those are
// sparse enough to miss on a spectrum and audible as crackle.
//
// So it converges instead, and almost always in two or three steps. The cap is
// what makes this safe in a realtime callback: the cost per sample is bounded
// whatever the signal does, and the loop cannot spin.
constexpr int kMaxNewtonIterations = 16;

// The target, in volts, and it sits deliberately at the float noise floor: ask
// for 1e-6 and nothing ever reaches it, ask for 1e-4 and the output is 24 dB
// worse against a converged reference. At 1e-5 a note lands within 0.003 V of
// that reference everywhere.
constexpr float kNewtonTolerance = 1.0e-5f;

// Ending a sample at the cap with a step still larger than this means the solve
// genuinely gave up, rather than merely running out of float precision short of
// the target above. That distinction is the whole point of counting: at the
// target, roughly one sample in eight hundred stops improving before it gets
// there, and none of them are wrong. This is the threshold the counter uses.
constexpr float kUnconvergedStepVolts = 1.0e-3f;

// A flat clamp on the step does not work here. It bounds the damage from a bad
// step but it also bounds the good ones, so convergence goes linear and the
// solve needs a dozen iterations to stay stable — which this circuit cannot
// afford. Limiting in the log domain instead keeps the step proportional to how
// far the exponential has actually moved, which is what SPICE does and what
// keeps convergence quadratic.
//
// Where limiting starts. The textbook critical voltage is a property of the
// device alone, and using it here was the whole problem: these transistors run
// at 0.47 V, but the network multiplies their current by about a million on the
// way to the port voltage, so by 0.70 V a step already moves the residual by
// thousands of volts — while the device-only threshold of 0.74 V has still not
// engaged. One unlimited step then throws the solve somewhere it takes a dozen
// iterations to walk back from.
//
// So the threshold is derived from the network instead: the voltage at which
// this port's current would swing the system by more than the supply could. Past
// that the step is not an answer, it is an overshoot.
constexpr float kHeadroomVolts = 10.0f;

float criticalVolts(float saturationCurrent, float thermalVolts, float portGainOhms)
{
  const float current = kHeadroomVolts / std::max(portGainOhms, 1.0f);
  return thermalVolts * std::log1p(current / saturationCurrent);
}

// How far a junction may be driven off in one step. Below a few thermal volts
// it conducts nothing and there is no information in going further, so a step
// that proposes volts of reverse bias is an overshoot rather than an answer.
// Leaving it unbounded is what used to cost this solve most of its iterations:
// one step would throw a port to several volts negative, and the limiting above
// then walked it back a fraction at a time.
constexpr float kMaxReverseStepVolts = 0.5f;

float limitJunction(float proposed, float previous, float thermalVolts, float critical)
{
  if (proposed < previous - kMaxReverseStepVolts) return previous - kMaxReverseStepVolts;
  if (proposed <= critical || std::fabs(proposed - previous) <= 2.0f * thermalVolts) {
    return proposed;
  }
  if (previous > 0.0f) {
    const float arg = 1.0f + (proposed - previous) / thermalVolts;
    return arg > 0.0f ? previous + thermalVolts * std::log(arg) : critical;
  }
  return thermalVolts * std::log(proposed / thermalVolts);
}

float onePoleCoefficient(double hz, double sampleRate)
{
  if (!(hz > 0.0) || !(sampleRate > 0.0)) return 1.0f;
  return static_cast<float>(std::clamp(1.0 - std::exp(-kTwoPi * hz / sampleRate), 0.0, 1.0));
}

// Solves a 3x3 system in place by Gaussian elimination with partial pivoting.
// Hand-rolled because three is small enough that a loop over a general solver
// would cost more than the arithmetic. Pivoting is required at control-range
// endpoints even though the usual playing range normally keeps the same rows.
void solve3(float m[3][3], float rhs[3])
{
  const float a = m[0][0], b = m[0][1], c = m[0][2];
  const float d = m[1][0], e = m[1][1], f = m[1][2];
  const float g = m[2][0], h = m[2][1], i = m[2][2];
  const float c00 = e * i - f * h;
  const float c01 = c * h - b * i;
  const float c02 = b * f - c * e;
  const float determinant = a * c00 + d * c01 + g * c02;
  if (std::isfinite(determinant) && std::fabs(determinant) >= 1.0e-20f) {
    const float r0 = rhs[0], r1 = rhs[1], r2 = rhs[2];
    const float reciprocal = 1.0f / determinant;
    rhs[0] = (c00 * r0 + c01 * r1 + c02 * r2) * reciprocal;
    rhs[1] = ((f * g - d * i) * r0 + (a * i - c * g) * r1
              + (c * d - a * f) * r2) * reciprocal;
    rhs[2] = ((d * h - e * g) * r0 + (b * g - a * h) * r1
              + (a * e - b * d) * r2) * reciprocal;
    return;
  }

  // The direct inverse above is cheaper and is well-conditioned through the
  // normal control range. Retain partial pivoting for a pathological Jacobian
  // rather than allowing a near-zero determinant to poison the audio state.
  int pivot = std::fabs(m[1][0]) > std::fabs(m[0][0]) ? 1 : 0;
  if (std::fabs(m[2][0]) > std::fabs(m[pivot][0])) pivot = 2;
  if (pivot != 0) {
    std::swap(m[0][0], m[pivot][0]);
    std::swap(m[0][1], m[pivot][1]);
    std::swap(m[0][2], m[pivot][2]);
    std::swap(rhs[0], rhs[pivot]);
  }

  const float d0 = m[0][0];
  if (std::fabs(d0) < 1.0e-30f) {
    rhs[0] = rhs[1] = rhs[2] = 0.0f;
    return;
  }
  const float f10 = m[1][0] / d0;
  const float f20 = m[2][0] / d0;
  m[1][1] -= f10 * m[0][1];
  m[1][2] -= f10 * m[0][2];
  rhs[1] -= f10 * rhs[0];
  m[2][1] -= f20 * m[0][1];
  m[2][2] -= f20 * m[0][2];
  rhs[2] -= f20 * rhs[0];

  if (std::fabs(m[2][1]) > std::fabs(m[1][1])) {
    std::swap(m[1][1], m[2][1]);
    std::swap(m[1][2], m[2][2]);
    std::swap(rhs[1], rhs[2]);
  }
  const float d1 = m[1][1];
  if (std::fabs(d1) < 1.0e-30f) {
    rhs[0] = rhs[1] = rhs[2] = 0.0f;
    return;
  }
  const float f21 = m[2][1] / d1;
  m[2][2] -= f21 * m[1][2];
  rhs[2] -= f21 * rhs[1];

  const float d2 = m[2][2];
  if (std::fabs(d2) < 1.0e-30f) {
    rhs[0] = rhs[1] = rhs[2] = 0.0f;
    return;
  }
  rhs[2] /= d2;
  rhs[1] = (rhs[1] - m[1][2] * rhs[2]) / d1;
  rhs[0] = (rhs[0] - m[0][1] * rhs[1] - m[0][2] * rhs[2]) / d0;
}

} // namespace

CheesePreparedMatrices prepareCheeseCircuitMatrices(const CheeseNetlist& netlist,
                                                     float fuzz, float tone,
                                                     float sampleRate)
{
  const auto derived = deriveCheeseDk(netlist, {fuzz, tone}, static_cast<double>(sampleRate));
  if (derived.states != kCheeseStateCount || derived.ports != kCheesePortCount
      || derived.inputs != kCheeseInputCount) {
    throw std::runtime_error("unexpected Big Cheese matrix dimensions");
  }

  CheesePreparedMatrices out;
  const auto copyFloats = [](auto& destination, const auto& source) {
    if (destination.size() != source.size()) {
      throw std::runtime_error("unexpected Big Cheese matrix storage size");
    }
    std::transform(source.begin(), source.end(), destination.begin(),
                   [](double value) { return static_cast<float>(value); });
  };
  copyFloats(out.a, derived.a);
  copyFloats(out.b, derived.b);
  copyFloats(out.c, derived.c);
  copyFloats(out.d, derived.d);
  copyFloats(out.e, derived.e);
  copyFloats(out.f, derived.f);
  copyFloats(out.g, derived.g);
  copyFloats(out.h, derived.h);
  copyFloats(out.k, derived.k);
  copyFloats(out.portVoltage, derived.portVoltage);
  out.outputOffset = static_cast<float>(derived.outputOffset);

  const float bjtVt =
    static_cast<float>(netlist.bjtEmissionCoefficient * netlist.thermalVolts);
  const float diodeVt =
    static_cast<float>(netlist.diodeEmissionCoefficient * netlist.thermalVolts);
  for (std::size_t r = 0; r < out.critical.size(); ++r) {
    float gain = 0.0f;
    for (std::size_t c = 0; c < kCheesePortCount; ++c) {
      gain += std::fabs(out.f[r * kCheesePortCount + c]);
    }
    out.critical[r] = criticalVolts(
      static_cast<float>(r < 2 ? netlist.bjtSaturationCurrent
                               : netlist.diodeSaturationCurrent),
      r < 2 ? bjtVt : diodeVt, gain);
  }

  // At DC the capacitors' trapezoidal states satisfy x = A*x + B*u + C*i.
  // Solve that system once here instead of advancing 384,000 silent samples
  // every time a preset is prepared.
  float dcCurrent[kCheesePortCount]{};
  for (std::size_t r = 0; r < 2; ++r) {
    const float e = std::exp(std::clamp(out.portVoltage[r] / bjtVt, -80.0f, 80.0f));
    dcCurrent[r] = static_cast<float>(netlist.bjtSaturationCurrent) * (e - 1.0f);
  }
  const float clipArg = std::clamp(out.portVoltage[2] / diodeVt, -80.0f, 80.0f);
  const float clipForward = std::exp(clipArg);
  dcCurrent[2] = static_cast<float>(netlist.diodeSaturationCurrent)
    * ((clipForward - 1.0f)
       - static_cast<float>(netlist.clipperJunctionCount) * (1.0f / clipForward - 1.0f));

  circuit::Mat steadyMatrix(kCheeseStateCount, kCheeseStateCount);
  circuit::Mat steadyRhs(kCheeseStateCount, 1);
  for (std::size_t r = 0; r < kCheeseStateCount; ++r) {
    for (std::size_t c = 0; c < kCheeseStateCount; ++c) {
      steadyMatrix.at(r, c) = (r == c ? 1.0 : 0.0) - out.a[r * kCheeseStateCount + c];
    }
    double rhs = out.b[r * kCheeseInputCount + 1] * static_cast<float>(netlist.supplyVolts);
    for (std::size_t c = 0; c < kCheesePortCount; ++c) {
      rhs += out.c[r * kCheesePortCount + c] * dcCurrent[c];
    }
    steadyRhs.at(r, 0) = rhs;
  }
  const auto steady = circuit::solve(std::move(steadyMatrix), std::move(steadyRhs));
  for (std::size_t r = 0; r < kCheeseStateCount; ++r) {
    out.equilibriumState[r] = static_cast<float>(steady.at(r, 0));
  }
  float stageOutput = out.h[1] * static_cast<float>(netlist.supplyVolts) - out.outputOffset;
  for (std::size_t r = 0; r < kCheesePortCount; ++r) stageOutput += out.k[r] * dcCurrent[r];
  for (std::size_t r = 0; r < kCheeseStateCount; ++r) {
    stageOutput += out.g[r] * out.equilibriumState[r];
  }
  out.equilibriumStageOutput = stageOutput;
  return out;
}

void CheeseCircuit::init(const CheeseNetlist& netlist, float sampleRate,
                         float fuzz, float tone, float volume)
{
  netlist_ = netlist;
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 192000.0f;
  fuzz_ = std::clamp(fuzz, 0.0f, 1.0f);
  tone_ = std::clamp(tone, 0.0f, 1.0f);
  volume_ = std::clamp(volume, 0.0f, 1.0f);
  volumeGain_ = static_cast<float>(
    std::pow(static_cast<double>(volume_), netlist_.taperExponent));
  const double rate = static_cast<double>(sampleRate_);

  bjtVt_ = static_cast<float>(netlist_.bjtEmissionCoefficient * netlist_.thermalVolts);
  diodeVt_ = static_cast<float>(netlist_.diodeEmissionCoefficient * netlist_.thermalVolts);
  bjtIs_ = static_cast<float>(netlist_.bjtSaturationCurrent);
  diodeIs_ = static_cast<float>(netlist_.diodeSaturationCurrent);
  clipperJunctions_ = static_cast<float>(netlist_.clipperJunctionCount);
  rail_ = static_cast<float>(netlist_.supplyVolts);

  // The input network ahead of the buffer: a DC block into the bias divider,
  // then a series resistor into a shunt cap that keeps radio out.
  const double biasOhms = netlist_.r3Ohms * netlist_.r4Ohms / (netlist_.r3Ohms + netlist_.r4Ohms);
  inputHighPassCoeff_ = onePoleCoefficient(1.0 / (kTwoPi * biasOhms * netlist_.c3Farads), rate);
  inputLowPassCoeff_ =
    onePoleCoefficient(1.0 / (kTwoPi * netlist_.r2Ohms * netlist_.c4Farads), rate);

  // The output stage. Trapezoidal companions, exactly as in the RAT's gain
  // stage, but with no nonlinearity: at these levels the AD712 is fast enough
  // and far enough from its rails to treat as ideal.
  const double halfStep = 0.5 / rate;
  c10Conductance_ = static_cast<float>(netlist_.c10Farads / halfStep);
  c11Conductance_ = static_cast<float>(netlist_.c11Farads / halfStep);
  feedbackConductance_ = static_cast<float>(1.0 / netlist_.r17Ohms);
  leg18Conductance_ = static_cast<float>(1.0 / (netlist_.r18Ohms + halfStep / netlist_.c11Farads));
  feedbackSlope_ = (feedbackConductance_ + c10Conductance_)
    / (feedbackConductance_ + c10Conductance_ + leg18Conductance_);

  // R19 into the volume track, then the DC block below it.
  outputDivider_ = static_cast<float>(
    netlist_.volumeTrackOhms / (netlist_.volumeTrackOhms + netlist_.r19Ohms));
  outputHighPassCoeff_ = onePoleCoefficient(
    1.0 / (kTwoPi * netlist_.volumeTrackOhms * netlist_.c13Farads), rate);

  matrices_ = prepareCheeseCircuitMatrices(netlist_, fuzz_, tone_, sampleRate_);
  reset();
}

void CheeseCircuit::applyPreparedMatrices(const CheesePreparedMatrices& matrices,
                                          float fuzz, float tone) noexcept
{
  matrices_ = matrices;
  fuzz_ = std::clamp(fuzz, 0.0f, 1.0f);
  tone_ = std::clamp(tone, 0.0f, 1.0f);
}

void CheeseCircuit::reset()
{
  inputHighPassState_ = 0.0f;
  inputLowPassState_ = 0.0f;
  c10History_ = 0.0f;
  leg18History_ = matrices_.equilibriumStageOutput;
  outputHighPassState_ = matrices_.equilibriumStageOutput;
  clipperVolts_ = 0.0f;
  unconverged_ = 0;
  processedSamples_ = 0;
  newtonIterations_ = 0;
  maxNewtonIterations_ = 0;
  newtonIterationHistogram_.fill(0);
  cappedStepHistogram_.fill(0);
  state_ = matrices_.equilibriumState;
  std::fill(scratch_.begin(), scratch_.end(), 0.0f);
  for (std::size_t i = 0; i < portVolts_.size(); ++i) {
    portVolts_[i] = matrices_.portVoltage[i];
  }

  // The prepared state is the exact discrete-time DC solution, so reset starts
  // close to the float runtime fixed point. A short polish removes coefficient
  // rounding residuals without simulating seconds of capacitor charging.
  constexpr std::size_t kResetPolishSamples = 4096;
  for (std::size_t i = 0; i < kResetPolishSamples; ++i) (void)process(0.0f);
  unconverged_ = 0;
  processedSamples_ = 0;
  newtonIterations_ = 0;
  maxNewtonIterations_ = 0;
  newtonIterationHistogram_.fill(0);
  cappedStepHistogram_.fill(0);
}

void CheeseCircuit::setControls(float fuzz, float tone, float volume)
{
  fuzz = std::clamp(fuzz, 0.0f, 1.0f);
  tone = std::clamp(tone, 0.0f, 1.0f);
  volume = std::clamp(volume, 0.0f, 1.0f);
  volumeGain_ = static_cast<float>(
    std::pow(static_cast<double>(volume), netlist_.taperExponent));
  if (fuzz == fuzz_ && tone == tone_) return;
  const auto prepared = prepareCheeseCircuitMatrices(netlist_, fuzz, tone, sampleRate_);
  applyPreparedMatrices(prepared, fuzz, tone);
}

float CheeseCircuit::process(float input)
{
  const float sample = std::isfinite(input) ? input : 0.0f;

  // --- Input network and buffer ------------------------------------------
  inputHighPassState_ += inputHighPassCoeff_ * (sample - inputHighPassState_);
  const float coupled = sample - inputHighPassState_;
  inputLowPassState_ += inputLowPassCoeff_ * (coupled - inputLowPassState_);
  const float buffered = inputLowPassState_;

  // --- Nodal section ------------------------------------------------------
  //
  // p = D x + E u is the port voltage the network would have with the devices
  // drawing nothing. The devices then satisfy v = p + F i and i = g(v), which
  // is one fixed point solved below.
  float p[3];
  for (int r = 0; r < 3; ++r) {
    float sum = matrices_.e[static_cast<std::size_t>(r) * 2 + 0] * buffered
      + matrices_.e[static_cast<std::size_t>(r) * 2 + 1] * rail_;
    for (std::size_t j = 0; j < kCheeseStateCount; ++j) {
      sum += matrices_.d[static_cast<std::size_t>(r) * kCheeseStateCount + j] * state_[j];
    }
    p[r] = sum;
  }

  float current[3]{};
  bool converged = false;
  float lastStep = 0.0f;
  unsigned iterationsUsed = 0;
  for (int iteration = 0; iteration < kMaxNewtonIterations; ++iteration) {
    ++iterationsUsed;
    float slope[3];
    // Q1 and Q2 carry a base current; the clipper carries a node current with a
    // junction facing each way, one of them doubled by Q3's paired junctions.
    for (int r = 0; r < 2; ++r) {
      const float arg = std::clamp(portVolts_[static_cast<std::size_t>(r)] / bjtVt_, -80.0f, 80.0f);
      const float e = std::exp(arg);
      current[r] = bjtIs_ * (e - 1.0f);
      slope[r] = (bjtIs_ / bjtVt_) * e;
    }
    {
      const float arg = std::clamp(portVolts_[2] / diodeVt_, -80.0f, 80.0f);
      const float forward = std::exp(arg);
      const float reverse = 1.0f / forward;
      current[2] = diodeIs_ * ((forward - 1.0f) - clipperJunctions_ * (reverse - 1.0f));
      slope[2] = (diodeIs_ / diodeVt_) * (forward + clipperJunctions_ * reverse);
    }

    // Residual of v - p - F g(v), and its Jacobian I - F diag(g').
    float jacobian[3][3];
    float residual[3];
    for (int r = 0; r < 3; ++r) {
      float sum = portVolts_[static_cast<std::size_t>(r)] - p[r];
      for (int c = 0; c < 3; ++c) {
        const float f = static_cast<float>(matrices_.f[static_cast<std::size_t>(r) * 3 + c]);
        sum -= f * current[c];
        jacobian[r][c] = (r == c ? 1.0f : 0.0f) - f * slope[c];
      }
      residual[r] = sum;
    }

    solve3(jacobian, residual);
    float largestStep = 0.0f;
    for (int r = 0; r < 3; ++r) {
      auto& port = portVolts_[static_cast<std::size_t>(r)];
      const float previous = port;
      const float step = std::isfinite(residual[r]) ? residual[r] : 0.0f;
      const float proposed = port - step;
      if (r < 2) {
        port = limitJunction(proposed, port, bjtVt_, matrices_.critical[static_cast<std::size_t>(r)]);
      } else {
        // The clipping node conducts either way round, so it is limited on
        // magnitude and the sign put back. Removing this limiting was tried, on
        // the reasoning that the node is already held inside a volt by its own
        // devices; the model then diverged during settling at every iteration
        // count. It is load bearing.
        const float sign = proposed < 0.0f ? -1.0f : 1.0f;
        port = sign * limitJunction(std::fabs(proposed), std::fabs(port),
                                    diodeVt_, matrices_.critical[2]);
      }
      largestStep = std::max(largestStep, std::fabs(port - previous));
    }
    if (largestStep < kNewtonTolerance) {
      converged = true;
      break;
    }
    lastStep = largestStep;
  }
  ++processedSamples_;
  newtonIterations_ += iterationsUsed;
  maxNewtonIterations_ = std::max(maxNewtonIterations_, iterationsUsed);
  ++newtonIterationHistogram_[iterationsUsed];
  if (iterationsUsed == kMaxNewtonIterations) {
    const float limits[] = {2.0e-5f, 5.0e-5f, 1.0e-4f, 3.0e-4f, 1.0e-3f, 3.0e-3f};
    std::size_t bucket = 0;
    while (bucket < std::size(limits) && lastStep >= limits[bucket]) ++bucket;
    ++cappedStepHistogram_[bucket];
  }
  if (!converged && lastStep > kUnconvergedStepVolts) ++unconverged_;

  // y = G x + H u + K i
  float out = matrices_.h[0] * buffered + matrices_.h[1] * rail_;
  for (int r = 0; r < 3; ++r) out += matrices_.k[static_cast<std::size_t>(r)] * current[r];
  for (std::size_t j = 0; j < kCheeseStateCount; ++j) out += matrices_.g[j] * state_[j];
  const float stageOut = out - matrices_.outputOffset;
  clipperVolts_ = portVolts_[2];

  // x' = A x + B u + C i
  for (std::size_t r = 0; r < kCheeseStateCount; ++r) {
    float sum = matrices_.b[r * 2 + 0] * buffered + matrices_.b[r * 2 + 1] * rail_;
    const float* row = matrices_.a.data() + r * kCheeseStateCount;
    for (std::size_t c = 0; c < kCheeseStateCount; ++c) sum += row[c] * state_[c];
    for (int c = 0; c < 3; ++c) sum += matrices_.c[r * 3 + static_cast<std::size_t>(c)] * current[c];
    scratch_[r] = sum;
  }
  state_.swap(scratch_);

  // --- Output stage -------------------------------------------------------
  //
  // An ideal op-amp holds its inverting input at the non-inverting one, so the
  // output follows from the feedback divider rather than from a loop solve.
  const float offset =
    (leg18Conductance_ * leg18History_ - c10Conductance_ * c10History_)
    / (feedbackConductance_ + c10Conductance_ + leg18Conductance_);
  const float amplified = (stageOut - offset) / feedbackSlope_;

  const float legCurrent = (stageOut - leg18History_) * leg18Conductance_;
  leg18History_ = stageOut - legCurrent * static_cast<float>(netlist_.r18Ohms)
    + legCurrent / c11Conductance_;
  c10History_ = 2.0f * (amplified - stageOut) - c10History_;

  outputHighPassState_ += outputHighPassCoeff_ * (amplified - outputHighPassState_);
  const float coupledOut = (amplified - outputHighPassState_) * outputDivider_;

  const float result = coupledOut * volumeGain_;
  return std::isfinite(result) ? result : 0.0f;
}

} // namespace ardor
