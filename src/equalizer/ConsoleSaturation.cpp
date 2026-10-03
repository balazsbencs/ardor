#include "equalizer/ConsoleSaturation.h"
#include <cmath>
#include <numbers>

namespace ardor {
namespace {
// Drive reaches 8 at full saturation. A sine peaking at kUnityLevel (-12 dBFS)
// keeps its mean peak level at any drive, so saturation adds color rather
// than a level drop.
constexpr double kMaxDrive = 8, kUnityLevel = 0.25, kMinDrive = 1e-2;
constexpr double kAdaaEpsilon = 1e-6;
// Console character. The bias is in input units: at full drive it shifts the
// curve by kMaxDrive * kConsoleBias = 0.64 of its knee.
constexpr double kConsoleBias = 0.08;
constexpr double kMaxTransformerDrive = 3, kTransformerHz = 100;
constexpr double kResidueDcHz = 5;

// log(cosh(u)) without overflow, and without cancellation for small u.
double logCosh(double u) {
  u = std::abs(u);
  if (u < 10) {
    const double s = std::sinh(0.5 * u);
    return std::log1p(2 * s * s);
  }
  return u - std::numbers::ln2 + std::log1p(std::exp(-2 * u));
}
} // namespace

void ConsoleSaturation::configure(float hostRate) {
  const double oversampledRate = 2.0 * hostRate;
  residueBlock_ = 1 - 2 * std::numbers::pi * kResidueDcHz / oversampledRate;
  lowBandStep_ = 1 - std::exp(-2 * std::numbers::pi * kTransformerHz / hostRate);
  drive_ = transformerDrive_ = bias_ = biasOffset_ = 0;
  makeup_ = 1;
}

void ConsoleSaturation::setAmount(float saturation, float console, std::span<State> states) {
  const double square = static_cast<double>(saturation) * saturation;
  drive_ = kMaxDrive * square;
  transformerDrive_ = kMaxTransformerDrive * square * console;
  if (drive_ < kMinDrive) return;
  bias_ = kConsoleBias * console;
  biasOffset_ = std::tanh(drive_ * bias_);
  // Match the mean of both peaks, because the bias compresses one side sooner.
  makeup_ = 2 * kUnityLevel
    / (std::tanh(drive_ * (kUnityLevel + bias_)) - std::tanh(drive_ * (bias_ - kUnityLevel)));
  for (auto& state : states) state.previousIntegral = residueIntegral(state.previousInput);
}

// f(x) = makeup * (tanh(drive * (x + bias)) - tanh(drive * bias)), so f(0) = 0.
double ConsoleSaturation::residue(double x) const {
  return makeup_ * (std::tanh(drive_ * (x + bias_)) - biasOffset_) - x;
}
double ConsoleSaturation::residueIntegral(double x) const {
  return makeup_ * (logCosh(drive_ * (x + bias_)) / drive_ - biasOffset_ * x) - 0.5 * x * x;
}

double ConsoleSaturation::shape(State& state, double x) const {
  if (drive_ < kMinDrive) {
    // Let the residue DC blocker decay rather than hold a stale value.
    state.previousInput = x;
    state.residueOut = residueBlock_ * state.residueOut - state.residueIn;
    state.residueIn = 0;
    if (std::abs(state.residueOut) < 1e-20) state.residueOut = 0;
    return x + state.residueOut;
  }
  const double integral = residueIntegral(x);
  const double delta = x - state.previousInput;
  const double r = std::abs(delta) > kAdaaEpsilon
    ? (integral - state.previousIntegral) / delta
    : residue(0.5 * (x + state.previousInput));
  state.previousInput = x;
  state.previousIntegral = integral;
  // The biased curve rectifies a little; only the residue carries that DC.
  state.residueOut = r - state.residueIn + residueBlock_ * state.residueOut;
  state.residueIn = r;
  if (std::abs(state.residueOut) < 1e-20) state.residueOut = 0;
  return x + state.residueOut;
}

float ConsoleSaturation::transformer(State& state, float x) const {
  state.lowBand += lowBandStep_ * (x - state.lowBand);
  if (transformerDrive_ < kMinDrive) return x;
  // Compress only the low band, the way a core saturates on bass first.
  const double low = state.lowBand;
  return x + static_cast<float>(std::tanh(transformerDrive_ * low) / transformerDrive_ - low);
}

} // namespace ardor
