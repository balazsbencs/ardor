#include "cheese/CheeseCircuit.h"

#include "circuit/MnaMatrix.h"

#include <algorithm>
#include <cmath>

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
// would cost more than the arithmetic.
void solve3(float m[3][3], float rhs[3])
{
  for (int col = 0; col < 3; ++col) {
    int pivot = col;
    for (int row = col + 1; row < 3; ++row) {
      if (std::fabs(m[row][col]) > std::fabs(m[pivot][col])) pivot = row;
    }
    if (pivot != col) {
      for (int j = 0; j < 3; ++j) std::swap(m[col][j], m[pivot][j]);
      std::swap(rhs[col], rhs[pivot]);
    }
    const float diagonal = m[col][col];
    if (std::fabs(diagonal) < 1.0e-30f) continue;
    for (int row = col + 1; row < 3; ++row) {
      const float factor = m[row][col] / diagonal;
      if (factor == 0.0f) continue;
      for (int j = col; j < 3; ++j) m[row][j] -= factor * m[col][j];
      rhs[row] -= factor * rhs[col];
    }
  }
  for (int i = 2; i >= 0; --i) {
    float sum = rhs[i];
    for (int j = i + 1; j < 3; ++j) sum -= m[i][j] * rhs[j];
    rhs[i] = std::fabs(m[i][i]) < 1.0e-30f ? 0.0f : sum / m[i][i];
  }
}

} // namespace

void CheeseCircuit::init(const CheeseNetlist& netlist, float sampleRate)
{
  netlist_ = netlist;
  sampleRate_ = sampleRate > 0.0f ? sampleRate : 192000.0f;
  const double rate = static_cast<double>(sampleRate_);

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

  dirty_ = true;
  rebuild();
  reset();
}

void CheeseCircuit::rebuild()
{
  if (!dirty_) return;
  dirty_ = false;
  matrices_ = deriveCheeseDk(netlist_, {fuzz_, tone_}, static_cast<double>(sampleRate_));

  // The limiting threshold depends on how hard each port drives the network, so
  // it is recomputed whenever the matrices are.
  const float bjtVt =
    static_cast<float>(netlist_.bjtEmissionCoefficient * netlist_.thermalVolts);
  const float diodeVt =
    static_cast<float>(netlist_.diodeEmissionCoefficient * netlist_.thermalVolts);
  for (std::size_t r = 0; r < critical_.size(); ++r) {
    double gain = 0.0;
    for (std::size_t c = 0; c < 3; ++c) gain += std::fabs(matrices_.f[r * 3 + c]);
    critical_[r] = criticalVolts(
      static_cast<float>(r < 2 ? netlist_.bjtSaturationCurrent : netlist_.diodeSaturationCurrent),
      r < 2 ? bjtVt : diodeVt, static_cast<float>(gain));
  }
  state_.assign(matrices_.states, 0.0f);
  scratch_.assign(matrices_.states, 0.0f);
  for (std::size_t i = 0; i < portVolts_.size(); ++i) {
    portVolts_[i] = static_cast<float>(matrices_.portVoltage[i]);
  }
}

void CheeseCircuit::reset()
{
  inputHighPassState_ = 0.0f;
  inputLowPassState_ = 0.0f;
  c10History_ = 0.0f;
  leg18History_ = 0.0f;
  outputHighPassState_ = 0.0f;
  clipperVolts_ = 0.0f;
  unconverged_ = 0;
  std::fill(state_.begin(), state_.end(), 0.0f);
  std::fill(scratch_.begin(), scratch_.end(), 0.0f);
  for (std::size_t i = 0; i < portVolts_.size(); ++i) {
    portVolts_[i] = static_cast<float>(matrices_.portVoltage[i]);
  }

  // Settle the bias network. The model carries the supply rail as an input, so
  // from a zeroed state the coupling caps charge over a real time constant —
  // the 4.7 uF across the Fuzz pot alone runs to seconds. Running that silently
  // here means the first note after a preset change is not a thump.
  const auto settle = static_cast<std::size_t>(sampleRate_);
  for (std::size_t i = 0; i < settle; ++i) (void)process(0.0f);
}

void CheeseCircuit::setControls(float fuzz, float tone, float volume)
{
  fuzz = std::clamp(fuzz, 0.0f, 1.0f);
  tone = std::clamp(tone, 0.0f, 1.0f);
  volume = std::clamp(volume, 0.0f, 1.0f);
  volumeGain_ = static_cast<float>(
    std::pow(static_cast<double>(volume), netlist_.taperExponent));
  if (fuzz == fuzz_ && tone == tone_) return;
  fuzz_ = fuzz;
  tone_ = tone;
  dirty_ = true;
  rebuild();
}

float CheeseCircuit::process(float input)
{
  const float sample = std::isfinite(input) ? input : 0.0f;
  const std::size_t states = matrices_.states;

  // --- Input network and buffer ------------------------------------------
  inputHighPassState_ += inputHighPassCoeff_ * (sample - inputHighPassState_);
  const float coupled = sample - inputHighPassState_;
  inputLowPassState_ += inputLowPassCoeff_ * (coupled - inputLowPassState_);
  const float buffered = inputLowPassState_;

  const float rail = static_cast<float>(netlist_.supplyVolts);

  // --- Nodal section ------------------------------------------------------
  //
  // p = D x + E u is the port voltage the network would have with the devices
  // drawing nothing. The devices then satisfy v = p + F i and i = g(v), which
  // is one fixed point solved below.
  float p[3];
  for (int r = 0; r < 3; ++r) {
    double sum = matrices_.e[static_cast<std::size_t>(r) * 2 + 0] * buffered
      + matrices_.e[static_cast<std::size_t>(r) * 2 + 1] * rail;
    for (std::size_t j = 0; j < states; ++j) {
      sum += matrices_.d[static_cast<std::size_t>(r) * states + j] * state_[j];
    }
    p[r] = static_cast<float>(sum);
  }

  const float bjtVt =
    static_cast<float>(netlist_.bjtEmissionCoefficient * netlist_.thermalVolts);
  const float diodeVt =
    static_cast<float>(netlist_.diodeEmissionCoefficient * netlist_.thermalVolts);
  const float bjtIs = static_cast<float>(netlist_.bjtSaturationCurrent);
  const float diodeIs = static_cast<float>(netlist_.diodeSaturationCurrent);
  const float junctions = static_cast<float>(netlist_.clipperJunctionCount);

  float current[3]{};
  bool converged = false;
  float lastStep = 0.0f;
  for (int iteration = 0; iteration < kMaxNewtonIterations; ++iteration) {
    float slope[3];
    // Q1 and Q2 carry a base current; the clipper carries a node current with a
    // junction facing each way, one of them doubled by Q3's paired junctions.
    for (int r = 0; r < 2; ++r) {
      const float arg = std::clamp(portVolts_[static_cast<std::size_t>(r)] / bjtVt, -80.0f, 80.0f);
      const float e = std::exp(arg);
      current[r] = bjtIs * (e - 1.0f);
      slope[r] = (bjtIs / bjtVt) * e;
    }
    {
      const float arg = std::clamp(portVolts_[2] / diodeVt, -80.0f, 80.0f);
      const float forward = std::exp(arg);
      const float reverse = 1.0f / forward;
      current[2] = diodeIs * ((forward - 1.0f) - junctions * (reverse - 1.0f));
      slope[2] = (diodeIs / diodeVt) * (forward + junctions * reverse);
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
        port = limitJunction(proposed, port, bjtVt, critical_[static_cast<std::size_t>(r)]);
      } else {
        // The clipping node conducts either way round, so it is limited on
        // magnitude and the sign put back. Removing this limiting was tried, on
        // the reasoning that the node is already held inside a volt by its own
        // devices; the model then diverged during settling at every iteration
        // count. It is load bearing.
        const float sign = proposed < 0.0f ? -1.0f : 1.0f;
        port = sign * limitJunction(std::fabs(proposed), std::fabs(port),
                                    diodeVt, critical_[2]);
      }
      largestStep = std::max(largestStep, std::fabs(port - previous));
    }
    if (largestStep < kNewtonTolerance) {
      converged = true;
      break;
    }
    lastStep = largestStep;
  }
  if (!converged && lastStep > kUnconvergedStepVolts) ++unconverged_;

  // y = G x + H u + K i
  double out = matrices_.h[0] * buffered + matrices_.h[1] * rail;
  for (int r = 0; r < 3; ++r) out += matrices_.k[static_cast<std::size_t>(r)] * current[r];
  for (std::size_t j = 0; j < states; ++j) out += matrices_.g[j] * state_[j];
  const float stageOut = static_cast<float>(out - matrices_.outputOffset);
  clipperVolts_ = portVolts_[2];

  // x' = A x + B u + C i
  for (std::size_t r = 0; r < states; ++r) {
    double sum = matrices_.b[r * 2 + 0] * buffered + matrices_.b[r * 2 + 1] * rail;
    const double* row = matrices_.a.data() + r * states;
    for (std::size_t c = 0; c < states; ++c) sum += row[c] * state_[c];
    for (int c = 0; c < 3; ++c) sum += matrices_.c[r * 3 + static_cast<std::size_t>(c)] * current[c];
    scratch_[r] = static_cast<float>(sum);
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
