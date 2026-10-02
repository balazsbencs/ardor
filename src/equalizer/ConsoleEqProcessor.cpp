#include "equalizer/ConsoleEqProcessor.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ardor {
namespace {
constexpr std::array<float, 5> kLowHz{0, 35, 60, 110, 220};
constexpr std::array<float, 7> kMidHz{0, 360, 700, 1600, 3200, 4800, 7200};
constexpr std::array<float, 5> kHighPassHz{0, 50, 80, 160, 300};
float safeValue(std::size_t i, float value) {
  const auto& c = kConsoleEqControls[i];
  value = std::clamp(value, c.minimum, c.maximum);
  return c.choices.empty() ? value : std::round(value);
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
  nlohmann::json params = {{"mode", "console_1073"}};
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
StereoSample ConsoleEqProcessor::Bank::process(StereoSample in) {
  for (auto& f : filters[0]) in.left = f.process(in.left);
  for (auto& f : filters[1]) in.right = f.process(in.right);
  return in;
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
  controlStep_ = 1 - std::exp(-2 * std::numbers::pi_v<float> * 10 * 32 / rate);
  sampleStep_ = 1 - std::exp(-1 / (0.01f * rate));
  fadeFrames_ = static_cast<std::size_t>(rate * 0.01f);
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
  const auto low = b.switches[0], mid = b.switches[1], hp = b.switches[2];
  const auto pass = hp ? makeHighPassCascade(sampleRate_, kHighPassHz[hp], 1, 18) : PassFilterCascade{};
  const std::array<BiquadCoefficients, 5> coeffs{
    pass.sections[0], pass.sections[1],
    low ? lowShelf(sampleRate_, kLowHz[low], current_[0]) : BiquadCoefficients{},
    mid ? makePeakingEq(sampleRate_, kMidHz[mid], 0.7f, current_[2]) : BiquadCoefficients{},
    makeHighShelf(sampleRate_, 12000, 0.70710678f, current_[4]),
  };
  for (auto& channel : b.filters)
    for (std::size_t i = 0; i < channel.size(); ++i) channel[i].c = coeffs[i];
}

void ConsoleEqProcessor::updateControls() {
  bool gainsChanged = false;
  for (std::size_t i = 0; i < current_.size(); ++i) {
    const float previous = current_[i];
    const float target = targets_->values[i].load(std::memory_order_relaxed);
    if (kConsoleEqControls[i].choices.empty()) {
      current_[i] += controlStep_ * (target - current_[i]);
      if (std::abs(current_[i] - target) < 1e-5f) current_[i] = target;
    } else current_[i] = target;
    if ((i == 0 || i == 2 || i == 4) && previous != current_[i]) gainsChanged = true;
  }
  outputTarget_ = std::pow(10.0f, current_[7] / 20);
  const std::array<int, 3> switches{static_cast<int>(current_[1]), static_cast<int>(current_[3]),
                                    static_cast<int>(current_[5])};
  // Complete the current fade before accepting another switch change. Rapid
  // automation queues the latest selection rather than repeatedly restarting.
  if (fadeRemaining_ == 0 && switches != banks_[active_].switches) {
    auto& next = banks_[1 - active_];
    next = {}; // A fresh cascade has no history from a different topology.
    next.switches = switches;
    fadeRemaining_ = fadeFrames_;
  }
  if (gainsChanged) updateBank(banks_[active_]);
  if (fadeRemaining_) updateBank(banks_[1 - active_]);
}

StereoSample ConsoleEqProcessor::process(StereoSample in) {
  if (!targets_) return in;
  if (controlCountdown_ == 0) {
    updateControls();
    controlCountdown_ = 32;
  }
  --controlCountdown_;
  const auto smooth = [this](float& value, float target) { value += sampleStep_ * (target - value); };
  smooth(saturation_, current_[6]);
  smooth(output_, outputTarget_);
  smooth(polarity_, 1 - 2 * current_[8]);
  smooth(mix_, current_[9]);
  std::array<float, 2> input{in.left, in.right}, saturated{}, dry{};
  const float amount = 16 * saturation_ * saturation_;
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    auto& c = channels_[i];
    dry[i] = c.dry[c.index];
    c.dry[c.index] = input[i];
    c.index = (c.index + 1) % kLatencyFrames;
    const auto up = c.up.Process(input[i]);
    for (float x : up) {
      // Unity small-signal slope. Amount zero is exactly the linear path.
      const float y = amount > 1e-5f ? std::tanh(amount * x) / amount : x;
      c.down.Push(y, saturated[i]);
    }
  }
  auto wet = banks_[active_].process({saturated[0], saturated[1]});
  if (fadeRemaining_) {
    const auto next = banks_[1 - active_].process({saturated[0], saturated[1]});
    const float blend = 1 - static_cast<float>(fadeRemaining_) / fadeFrames_;
    wet.left += blend * (next.left - wet.left);
    wet.right += blend * (next.right - wet.right);
    if (--fadeRemaining_ == 0) active_ = 1 - active_;
  }
  return {(1 - mix_) * dry[0] + mix_ * wet.left * output_ * polarity_,
          (1 - mix_) * dry[1] + mix_ * wet.right * output_ * polarity_};
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
  banks_[0].switches = {static_cast<int>(current_[1]), static_cast<int>(current_[3]), static_cast<int>(current_[5])};
  updateBank(banks_[0]);
  banks_[1] = banks_[0];
  saturation_ = current_[6];
  output_ = outputTarget_ = std::pow(10.0f, current_[7] / 20);
  polarity_ = 1 - 2 * current_[8];
  mix_ = current_[9];
}
} // namespace ardor
