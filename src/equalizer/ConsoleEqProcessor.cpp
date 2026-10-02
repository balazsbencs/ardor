#include "equalizer/ConsoleEqProcessor.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ardor {
namespace {
enum Control : std::size_t {
  kLowDb, kLowFreq, kMidDb, kMidFreq, kHighDb, kHighPass, kSaturation, kOutputDb, kPolarity, kMix,
};
// Switch positions inside Bank::switches, and the cascade sections each owns.
enum Switch : std::size_t { kLowSwitch, kMidSwitch, kHighPassSwitch };
constexpr std::size_t kHighPassFirst = 0, kHighPassLast = 1, kLowSection = 2, kMidSection = 3;

constexpr std::array<float, 5> kLowHz{0, 35, 60, 110, 220};
constexpr std::array<float, 7> kMidHz{0, 360, 700, 1600, 3200, 4800, 7200};
constexpr std::array<float, 5> kHighPassHz{0, 50, 80, 160, 300};
constexpr std::size_t kControlInterval = 32;
constexpr float kControlSmoothingHz = 10;
constexpr float kSampleSmoothingSeconds = 0.01f;
// A low or HPF corner settles far slower than the 10 ms used for the mid.
constexpr float kShortFadeSeconds = 0.01f, kLongFadeSeconds = 0.04f;
// Drive reaches 8 at full saturation. A sine peaking at kUnityLevel (-12 dBFS)
// keeps its peak level at any drive, so saturation adds color, not a level drop.
constexpr double kMaxDrive = 8, kUnityLevel = 0.25, kMinDrive = 1e-2;
constexpr double kAdaaEpsilon = 1e-6;

float safeValue(std::size_t i, float value) {
  const auto& c = kConsoleEqControls[i];
  value = std::clamp(value, c.minimum, c.maximum);
  return c.choices.empty() ? value : std::round(value);
}
float dbToGain(float db) { return std::pow(10.0f, db / 20); }
// log(cosh(u)) without overflow, and without cancellation for small u.
double logCosh(double u) {
  u = std::abs(u);
  if (u < 10) {
    const double s = std::sinh(0.5 * u);
    return std::log1p(2 * s * s);
  }
  return u - std::numbers::ln2 + std::log1p(std::exp(-2 * u));
}
BiquadCoefficients lowShelf(float rate, float hz, float gain) {
  const double a = std::pow(10.0, gain / 40.0);
  const double omega = 2 * std::numbers::pi * hz / rate;
  const double cosine = std::cos(omega);
  const double beta = std::sqrt(a) * std::sin(omega) / 0.70710678;
  const double denominator = (a + 1) + (a - 1) * cosine + beta;
  return {
    static_cast<float>(a * ((a + 1) - (a - 1) * cosine + beta) / denominator),
    static_cast<float>(2 * a * ((a - 1) - (a + 1) * cosine) / denominator),
    static_cast<float>(a * ((a + 1) - (a - 1) * cosine - beta) / denominator),
    static_cast<float>(-2 * ((a - 1) + (a + 1) * cosine) / denominator),
    static_cast<float>(((a + 1) + (a - 1) * cosine - beta) / denominator),
  };
}
}

nlohmann::json defaultConsoleEqParams() {
  nlohmann::json params = {{"mode", std::string(kConsoleEqMode)}};
  for (const auto& c : kConsoleEqControls) params[c.key] = c.defaultValue;
  return params;
}

float ConsoleEqProcessor::Biquad::process(float x) {
  const double y = c.b0 * static_cast<double>(x) + z1;
  z1 = c.b1 * static_cast<double>(x) - c.a1 * y + z2;
  z2 = c.b2 * static_cast<double>(x) - c.a2 * y;
  if (std::abs(z1) < 1e-24) z1 = 0;
  if (std::abs(z2) < 1e-24) z2 = 0;
  return static_cast<float>(y);
}
float ConsoleEqProcessor::Bank::process(std::size_t channel, float x) {
  for (auto& f : filters[channel]) x = f.process(x);
  return x;
}

bool ConsoleEqProcessor::configure(const nlohmann::json& params, float rate, std::string& error) {
  error.clear();
  if (!std::isfinite(rate) || rate < 32000 || rate > 192000) {
    error = "1073 EQ requires a sample rate between 32000 and 192000 Hz";
    return false;
  }
  if (!params.is_object()) {
    error = "1073 EQ parameters must be an object";
    return false;
  }
  auto next = std::make_shared<Targets>();
  for (std::size_t i = 0; i < kConsoleEqControls.size(); ++i) {
    const auto& c = kConsoleEqControls[i];
    const auto it = params.find(c.key);
    if (it != params.end() && (!it->is_number() || !std::isfinite(it->get<float>()))) {
      error = "1073 EQ parameter must be finite: " + std::string(c.key);
      return false;
    }
    next->values[i].store(safeValue(i, it == params.end() ? c.defaultValue : it->get<float>()));
  }
  targets_ = std::move(next);
  sampleRate_ = rate;
  controlStep_ = 1 - std::exp(-2 * std::numbers::pi_v<float> * kControlSmoothingHz * kControlInterval / rate);
  sampleStep_ = 1 - std::exp(-1 / (kSampleSmoothingSeconds * rate));
  shortFadeFrames_ = static_cast<std::size_t>(rate * kShortFadeSeconds);
  longFadeFrames_ = static_cast<std::size_t>(rate * kLongFadeSeconds);
  reset();
  return true;
}

bool ConsoleEqProcessor::setParameterTarget(std::string_view key, float value) {
  if (!targets_ || !std::isfinite(value)) return false;
  for (std::size_t i = 0; i < kConsoleEqControls.size(); ++i) {
    if (kConsoleEqControls[i].key == key) {
      targets_->values[i].store(safeValue(i, value), std::memory_order_relaxed);
      return true;
    }
  }
  return false;
}

void ConsoleEqProcessor::updateBank(Bank& b) {
  const auto low = b.switches[kLowSwitch], mid = b.switches[kMidSwitch], hp = b.switches[kHighPassSwitch];
  const auto pass = hp ? makeHighPassCascade(sampleRate_, kHighPassHz[hp], 1, 18) : PassFilterCascade{};
  const std::array<BiquadCoefficients, kSections> coeffs{
    pass.sections[0], pass.sections[1],
    low ? lowShelf(sampleRate_, kLowHz[low], current_[kLowDb]) : BiquadCoefficients{},
    mid ? makePeakingEq(sampleRate_, kMidHz[mid], 0.7f, current_[kMidDb]) : BiquadCoefficients{},
    makeHighShelf(sampleRate_, 12000, 0.70710678f, current_[kHighDb]),
  };
  for (auto& channel : b.filters)
    for (std::size_t i = 0; i < channel.size(); ++i) channel[i].c = coeffs[i];
}

void ConsoleEqProcessor::startFade(const std::array<int, 3>& switches) {
  const auto& now = banks_[active_];
  auto& next = banks_[1 - active_];
  // Sections whose switch is unchanged keep their history, so a mid change
  // does not restart the HPF and low shelf. Changed sections start silent.
  next = now;
  const auto restart = [&next](std::size_t first, std::size_t last) {
    for (auto& channel : next.filters)
      for (std::size_t i = first; i <= last; ++i) channel[i].z1 = channel[i].z2 = 0;
  };
  if (switches[kHighPassSwitch] != now.switches[kHighPassSwitch]) restart(kHighPassFirst, kHighPassLast);
  if (switches[kLowSwitch] != now.switches[kLowSwitch]) restart(kLowSection, kLowSection);
  if (switches[kMidSwitch] != now.switches[kMidSwitch]) restart(kMidSection, kMidSection);
  const bool lowCornerChanged = switches[kLowSwitch] != now.switches[kLowSwitch]
    || switches[kHighPassSwitch] != now.switches[kHighPassSwitch];
  next.switches = switches;
  updateBank(next);
  fadeLength_ = fadeRemaining_ = std::max<std::size_t>(1, lowCornerChanged ? longFadeFrames_ : shortFadeFrames_);
}

double ConsoleEqProcessor::residue(double x) const {
  return makeup_ * std::tanh(drive_ * x) - x;
}
double ConsoleEqProcessor::residueIntegral(double x) const {
  return makeup_ * logCosh(drive_ * x) / drive_ - 0.5 * x * x;
}

void ConsoleEqProcessor::updateDrive(float saturation) {
  drive_ = kMaxDrive * saturation * saturation;
  if (drive_ < kMinDrive) return;
  makeup_ = kUnityLevel / std::tanh(drive_ * kUnityLevel);
  // The antiderivative depends on the drive, so its history must follow it.
  for (auto& channel : channels_) channel.previousIntegral = residueIntegral(channel.previousInput);
}

double ConsoleEqProcessor::shape(Channel& channel, double x) const {
  if (drive_ < kMinDrive) {
    channel.previousInput = x;
    return x;
  }
  // Only the distortion residue goes through ADAA, so a light drive keeps the
  // exact linear path and its latency.
  const double integral = residueIntegral(x);
  const double delta = x - channel.previousInput;
  const double r = std::abs(delta) > kAdaaEpsilon
    ? (integral - channel.previousIntegral) / delta
    : residue(0.5 * (x + channel.previousInput));
  channel.previousInput = x;
  channel.previousIntegral = integral;
  return x + r;
}

void ConsoleEqProcessor::updateControls() {
  bool gainsChanged = false, saturationChanged = false;
  for (std::size_t i = 0; i < current_.size(); ++i) {
    const float previous = current_[i];
    const float target = targets_->values[i].load(std::memory_order_relaxed);
    if (kConsoleEqControls[i].choices.empty()) {
      current_[i] += controlStep_ * (target - current_[i]);
      if (std::abs(current_[i] - target) < 1e-5f) current_[i] = target;
    } else current_[i] = target;
    if (previous == current_[i]) continue;
    if (i == kLowDb || i == kMidDb || i == kHighDb) gainsChanged = true;
    if (i == kSaturation) saturationChanged = true;
  }
  outputTarget_ = dbToGain(current_[kOutputDb]);
  if (saturationChanged) updateDrive(current_[kSaturation]);
  const std::array<int, 3> switches{static_cast<int>(current_[kLowFreq]), static_cast<int>(current_[kMidFreq]),
                                    static_cast<int>(current_[kHighPass])};
  // Complete the current fade before accepting another switch change. Rapid
  // automation queues the latest selection rather than repeatedly restarting.
  if (fadeRemaining_ == 0 && switches != banks_[active_].switches) startFade(switches);
  else if (fadeRemaining_ && gainsChanged) updateBank(banks_[1 - active_]);
  if (gainsChanged) updateBank(banks_[active_]);
}

void ConsoleEqProcessor::beginFrame() {
  if (controlCountdown_ == 0) {
    updateControls();
    controlCountdown_ = kControlInterval;
  }
  --controlCountdown_;
  const auto smooth = [this](float& value, float target) { value += sampleStep_ * (target - value); };
  smooth(output_, outputTarget_);
  smooth(polarity_, 1 - 2 * current_[kPolarity]);
  smooth(mix_, current_[kMix]);
  blend_ = fadeRemaining_ ? 1 - static_cast<float>(fadeRemaining_) / fadeLength_ : 0;
}

float ConsoleEqProcessor::processChannel(std::size_t index, float input) {
  auto& c = channels_[index];
  const float dry = c.dry[c.index];
  c.dry[c.index] = input;
  c.index = (c.index + 1) % kLatencyFrames;
  float saturated = 0;
  for (float x : c.up.Process(input)) c.down.Push(static_cast<float>(shape(c, x)), saturated);
  float wet = banks_[active_].process(index, saturated);
  if (fadeRemaining_) wet += blend_ * (banks_[1 - active_].process(index, saturated) - wet);
  // Trim and polarity act on the whole output, so Mix never cancels the dry.
  return polarity_ * output_ * ((1 - mix_) * dry + mix_ * wet);
}

void ConsoleEqProcessor::endFrame() {
  if (fadeRemaining_ && --fadeRemaining_ == 0) active_ = 1 - active_;
}

void ConsoleEqProcessor::copyLeftToRight() {
  channels_[1] = channels_[0];
  for (auto& bank : banks_) bank.filters[1] = bank.filters[0];
  rightMirrorsLeft_ = false;
}

StereoSample ConsoleEqProcessor::process(StereoSample in) {
  if (!targets_) return in;
  if (rightMirrorsLeft_) copyLeftToRight();
  beginFrame();
  const StereoSample out{processChannel(0, in.left), processChannel(1, in.right)};
  endFrame();
  return out;
}

void ConsoleEqProcessor::processBlock(const float* inputLeft, const float* inputRight, float* outputLeft,
                                      float* outputRight, std::size_t frames, bool stereo) {
  if (!targets_ || stereo) {
    for (std::size_t i = 0; i < frames; ++i) {
      const auto y = process({inputLeft[i], inputRight[i]});
      outputLeft[i] = y.left;
      outputRight[i] = y.right;
    }
    return;
  }
  // A mono signal needs one channel of work. The right history is copied from
  // the left when stereo input returns.
  for (std::size_t i = 0; i < frames; ++i) {
    beginFrame();
    outputLeft[i] = outputRight[i] = processChannel(0, inputLeft[i]);
    endFrame();
  }
  if (frames > 0) rightMirrorsLeft_ = true;
}

void ConsoleEqProcessor::reset() {
  if (!targets_) return;
  for (std::size_t i = 0; i < current_.size(); ++i)
    current_[i] = targets_->values[i].load(std::memory_order_relaxed);
  banks_ = {};
  channels_ = {};
  // Select the even internal phase: the two FIRs then have exactly 15
  // host frames of delay, rather than a 14.5-frame fractional delay.
  for (auto& channel : channels_) {
    float ignored = 0;
    channel.down.Push(0, ignored);
  }
  active_ = 0;
  controlCountdown_ = fadeRemaining_ = 0;
  fadeLength_ = 1;
  blend_ = 0;
  rightMirrorsLeft_ = false;
  banks_[0].switches = {static_cast<int>(current_[kLowFreq]), static_cast<int>(current_[kMidFreq]),
                        static_cast<int>(current_[kHighPass])};
  updateBank(banks_[0]);
  banks_[1] = banks_[0];
  updateDrive(current_[kSaturation]);
  output_ = outputTarget_ = dbToGain(current_[kOutputDb]);
  polarity_ = 1 - 2 * current_[kPolarity];
  mix_ = current_[kMix];
}
} // namespace ardor
