#include "dynamics/NoiseGateProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ardor {

struct NoiseGateProcessor::LiveParameters {
  std::atomic<float> thresholdDb{-55.0f};
  std::atomic<float> reductionDb{80.0f};
  std::atomic<float> attackMs{2.0f};
  std::atomic<float> holdMs{50.0f};
  std::atomic<float> releaseMs{150.0f};
  std::atomic<float> hysteresisDb{6.0f};
  std::atomic<float> sidechainHpfHz{80.0f};
  std::atomic<std::uint64_t> revision{0};
};

namespace {

constexpr float kPi = 3.14159265358979323846f;

float dbToGain(float db)
{
  return std::pow(10.0f, db / 20.0f);
}

float configuredNumber(const nlohmann::json& params, const char* key, float fallback,
                       float minimum, float maximum)
{
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

float smoothingCoefficient(float milliseconds, float sampleRate)
{
  return std::exp(-1.0f / (milliseconds * 0.001f * sampleRate));
}

} // namespace

bool NoiseGateProcessor::configure(const nlohmann::json& params, float sampleRate,
                                   std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "noise gate sample rate must be finite and positive";
    return false;
  }

  sampleRate_ = sampleRate;
  const float thresholdDb = configuredNumber(params, "threshold_db", -55.0f, -80.0f, 0.0f);
  const float reductionDb = configuredNumber(params, "reduction_db", 80.0f, 0.0f, 96.0f);
  const float attackMs = configuredNumber(params, "attack_ms", 2.0f, 0.1f, 50.0f);
  const float holdMs = configuredNumber(params, "hold_ms", 50.0f, 0.0f, 500.0f);
  const float releaseMs = configuredNumber(params, "release_ms", 150.0f, 10.0f, 2000.0f);
  const float hysteresisDb = configuredNumber(params, "hysteresis_db", 6.0f, 0.0f, 18.0f);
  const float sidechainHpfHz =
    configuredNumber(params, "sidechain_hpf_hz", 80.0f, 20.0f, 500.0f);

  thresholdGain_ = dbToGain(thresholdDb);
  closeThresholdGain_ = dbToGain(thresholdDb - hysteresisDb);
  reductionGain_ = dbToGain(-reductionDb);
  attackCoefficient_ = smoothingCoefficient(attackMs, sampleRate_);
  releaseCoefficient_ = smoothingCoefficient(releaseMs, sampleRate_);
  sidechainHpfCoefficient_ = std::exp(-2.0f * kPi * sidechainHpfHz / sampleRate_);
  holdSamples_ = static_cast<std::uint64_t>(
    std::max(0.0, std::round(static_cast<double>(holdMs) * 0.001 * sampleRate_)));

  liveParameters_ = std::make_shared<LiveParameters>();
  liveParameters_->thresholdDb.store(thresholdDb);
  liveParameters_->reductionDb.store(reductionDb);
  liveParameters_->attackMs.store(attackMs);
  liveParameters_->holdMs.store(holdMs);
  liveParameters_->releaseMs.store(releaseMs);
  liveParameters_->hysteresisDb.store(hysteresisDb);
  liveParameters_->sidechainHpfHz.store(sidechainHpfHz);
  liveRevision_ = 1;
  liveParameters_->revision.store(liveRevision_, std::memory_order_release);
  reset();
  return true;
}

bool NoiseGateProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!liveParameters_ || !std::isfinite(value)) return false;
  std::atomic<float>* target = nullptr;
  if (key == "threshold_db") {
    value = std::clamp(value, -80.0f, 0.0f);
    target = &liveParameters_->thresholdDb;
  } else if (key == "reduction_db") {
    value = std::clamp(value, 0.0f, 96.0f);
    target = &liveParameters_->reductionDb;
  } else if (key == "attack_ms") {
    value = std::clamp(value, 0.1f, 50.0f);
    target = &liveParameters_->attackMs;
  } else if (key == "hold_ms") {
    value = std::clamp(value, 0.0f, 500.0f);
    target = &liveParameters_->holdMs;
  } else if (key == "release_ms") {
    value = std::clamp(value, 10.0f, 2000.0f);
    target = &liveParameters_->releaseMs;
  } else if (key == "hysteresis_db") {
    value = std::clamp(value, 0.0f, 18.0f);
    target = &liveParameters_->hysteresisDb;
  } else if (key == "sidechain_hpf_hz") {
    value = std::clamp(value, 20.0f, 500.0f);
    target = &liveParameters_->sidechainHpfHz;
  }
  if (!target) return false;
  target->store(value, std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
  return true;
}

void NoiseGateProcessor::refreshLiveParameters()
{
  if (!liveParameters_) return;
  const auto revision = liveParameters_->revision.load(std::memory_order_acquire);
  if (revision == liveRevision_) return;
  liveRevision_ = revision;

  const float thresholdDb = liveParameters_->thresholdDb.load(std::memory_order_relaxed);
  const float hysteresisDb = liveParameters_->hysteresisDb.load(std::memory_order_relaxed);
  thresholdGain_ = dbToGain(thresholdDb);
  closeThresholdGain_ = dbToGain(thresholdDb - hysteresisDb);
  reductionGain_ = dbToGain(-liveParameters_->reductionDb.load(std::memory_order_relaxed));
  attackCoefficient_ = smoothingCoefficient(
    liveParameters_->attackMs.load(std::memory_order_relaxed), sampleRate_);
  releaseCoefficient_ = smoothingCoefficient(
    liveParameters_->releaseMs.load(std::memory_order_relaxed), sampleRate_);
  sidechainHpfCoefficient_ = std::exp(
    -2.0f * kPi * liveParameters_->sidechainHpfHz.load(std::memory_order_relaxed) / sampleRate_);
  holdSamples_ = static_cast<std::uint64_t>(std::max(
    0.0, std::round(static_cast<double>(
      liveParameters_->holdMs.load(std::memory_order_relaxed)) * 0.001 * sampleRate_)));
  holdRemaining_ = std::min(holdRemaining_, holdSamples_);
}

void NoiseGateProcessor::reset()
{
  sidechainPreviousInputLeft_ = 0.0f;
  sidechainPreviousInputRight_ = 0.0f;
  sidechainPreviousOutputLeft_ = 0.0f;
  sidechainPreviousOutputRight_ = 0.0f;
  gain_ = reductionGain_;
  holdRemaining_ = 0;
  open_ = false;
}

float NoiseGateProcessor::highPassedLevel(float input, float& previousInput,
                                          float& previousOutput) const
{
  const float highPassed = input - previousInput + sidechainHpfCoefficient_ * previousOutput;
  previousInput = input;
  previousOutput = highPassed;
  return std::fabs(highPassed);
}

StereoSample NoiseGateProcessor::process(StereoSample input)
{
  refreshLiveParameters();
  const float detectorLevel = std::max(
    highPassedLevel(input.left, sidechainPreviousInputLeft_, sidechainPreviousOutputLeft_),
    highPassedLevel(input.right, sidechainPreviousInputRight_, sidechainPreviousOutputRight_));

  if (!open_) {
    if (detectorLevel >= thresholdGain_) {
      open_ = true;
      holdRemaining_ = holdSamples_;
    }
  } else if (detectorLevel >= closeThresholdGain_) {
    holdRemaining_ = holdSamples_;
  } else if (holdRemaining_ > 0) {
    --holdRemaining_;
  } else {
    open_ = false;
  }

  const float targetGain = open_ ? 1.0f : reductionGain_;
  const float coefficient = targetGain > gain_ ? attackCoefficient_ : releaseCoefficient_;
  gain_ = coefficient * gain_ + (1.0f - coefficient) * targetGain;
  if (std::fabs(gain_ - targetGain) < 1.0e-8f) gain_ = targetGain;
  return {input.left * gain_, input.right * gain_};
}

} // namespace ardor
