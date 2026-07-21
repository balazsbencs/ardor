#include "dynamics/CompressorProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ardor {

struct CompressorProcessor::LiveParameters {
  std::atomic<float> thresholdDb{-24.0f};
  std::atomic<float> ratio{4.0f};
  std::atomic<float> attackMs{10.0f};
  std::atomic<float> releaseMs{150.0f};
  std::atomic<float> kneeDb{6.0f};
  std::atomic<float> makeupDb{0.0f};
  std::atomic<float> inputGainDb{0.0f};
  std::atomic<float> mix{1.0f};
  std::atomic<float> sidechainHpfHz{80.0f};
  std::atomic<uint64_t> revision{0};
};

namespace {

constexpr float kPi = 3.14159265358979323846f;

float dbToGain(float db)
{
  return std::pow(10.0f, db / 20.0f);
}

float configuredNumber(const nlohmann::json& params, const char* key, float fallback, float minimum, float maximum)
{
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) {
    return fallback;
  }
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

} // namespace

bool CompressorProcessor::configure(const nlohmann::json& params, float sampleRate, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "compressor sample rate must be finite and positive";
    return false;
  }

  const std::string detector = params.value("detector", "peak");
  if (detector == "peak") {
    detector_ = Detector::Peak;
  } else if (detector == "rms") {
    detector_ = Detector::Rms;
  } else {
    error = "compressor detector must be peak or rms";
    return false;
  }

  sampleRate_ = sampleRate;
  thresholdDb_ = configuredNumber(params, "threshold_db", -24.0f, -60.0f, 0.0f);
  ratio_ = configuredNumber(params, "ratio", 4.0f, 1.0f, 20.0f);
  const float attackMs = configuredNumber(params, "attack_ms", 10.0f, 0.1f, 200.0f);
  const float releaseMs = configuredNumber(params, "release_ms", 150.0f, 10.0f, 2000.0f);
  kneeDb_ = configuredNumber(params, "knee_db", 6.0f, 0.0f, 24.0f);
  const float makeupDb = configuredNumber(params, "makeup_db", 0.0f, 0.0f, 24.0f);
  const float inputGainDb = configuredNumber(params, "input_gain_db", 0.0f, -24.0f, 24.0f);
  mix_ = configuredNumber(params, "mix", 1.0f, 0.0f, 1.0f);
  const float sidechainHpfHz = configuredNumber(params, "sidechain_hpf_hz", 80.0f, 20.0f, 500.0f);
  const bool autoMakeup = params.value("auto_makeup", false);

  attackCoefficient_ = std::exp(-1.0f / (attackMs * 0.001f * sampleRate));
  releaseCoefficient_ = std::exp(-1.0f / (releaseMs * 0.001f * sampleRate));
  sidechainHpfCoefficient_ = std::exp(-2.0f * kPi * sidechainHpfHz / sampleRate);
  inputGain_ = dbToGain(inputGainDb);
  const float automaticMakeupDb = autoMakeup ? std::min(24.0f, -thresholdDb_ * (1.0f - 1.0f / ratio_) * 0.5f) : 0.0f;
  makeupGain_ = dbToGain(std::min(24.0f, makeupDb + automaticMakeupDb));
  liveParameters_ = std::make_shared<LiveParameters>();
  liveParameters_->thresholdDb.store(thresholdDb_);
  liveParameters_->ratio.store(ratio_);
  liveParameters_->attackMs.store(attackMs);
  liveParameters_->releaseMs.store(releaseMs);
  liveParameters_->kneeDb.store(kneeDb_);
  liveParameters_->makeupDb.store(makeupDb);
  liveParameters_->inputGainDb.store(inputGainDb);
  liveParameters_->mix.store(mix_);
  liveParameters_->sidechainHpfHz.store(sidechainHpfHz);
  liveRevision_ = 1;
  liveParameters_->revision.store(liveRevision_, std::memory_order_release);
  reset();
  return true;
}

bool CompressorProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!liveParameters_ || !std::isfinite(value)) return false;
  std::atomic<float>* target = nullptr;
  if (key == "threshold_db") { value = std::clamp(value, -60.0f, 0.0f); target = &liveParameters_->thresholdDb; }
  else if (key == "ratio") { value = std::clamp(value, 1.0f, 20.0f); target = &liveParameters_->ratio; }
  else if (key == "attack_ms") { value = std::clamp(value, 0.1f, 200.0f); target = &liveParameters_->attackMs; }
  else if (key == "release_ms") { value = std::clamp(value, 10.0f, 2000.0f); target = &liveParameters_->releaseMs; }
  else if (key == "knee_db") { value = std::clamp(value, 0.0f, 24.0f); target = &liveParameters_->kneeDb; }
  else if (key == "makeup_db") { value = std::clamp(value, 0.0f, 24.0f); target = &liveParameters_->makeupDb; }
  else if (key == "input_gain_db") { value = std::clamp(value, -24.0f, 24.0f); target = &liveParameters_->inputGainDb; }
  else if (key == "mix") { value = std::clamp(value, 0.0f, 1.0f); target = &liveParameters_->mix; }
  else if (key == "sidechain_hpf_hz") { value = std::clamp(value, 20.0f, 500.0f); target = &liveParameters_->sidechainHpfHz; }
  if (!target) return false;
  target->store(value, std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
  return true;
}

void CompressorProcessor::refreshLiveParameters()
{
  if (!liveParameters_) return;
  const auto revision = liveParameters_->revision.load(std::memory_order_acquire);
  if (revision == liveRevision_) return;
  liveRevision_ = revision;
  thresholdDb_ = liveParameters_->thresholdDb.load(std::memory_order_relaxed);
  ratio_ = liveParameters_->ratio.load(std::memory_order_relaxed);
  kneeDb_ = liveParameters_->kneeDb.load(std::memory_order_relaxed);
  const float attackMs = liveParameters_->attackMs.load(std::memory_order_relaxed);
  const float releaseMs = liveParameters_->releaseMs.load(std::memory_order_relaxed);
  attackCoefficient_ = std::exp(-1.0f / (attackMs * 0.001f * sampleRate_));
  releaseCoefficient_ = std::exp(-1.0f / (releaseMs * 0.001f * sampleRate_));
  sidechainHpfCoefficient_ = std::exp(-2.0f * kPi * liveParameters_->sidechainHpfHz.load(std::memory_order_relaxed) / sampleRate_);
  inputGain_ = dbToGain(liveParameters_->inputGainDb.load(std::memory_order_relaxed));
  makeupGain_ = dbToGain(liveParameters_->makeupDb.load(std::memory_order_relaxed));
  mix_ = liveParameters_->mix.load(std::memory_order_relaxed);
}

void CompressorProcessor::reset()
{
  sidechainPreviousInputLeft_ = 0.0f;
  sidechainPreviousInputRight_ = 0.0f;
  sidechainPreviousOutputLeft_ = 0.0f;
  sidechainPreviousOutputRight_ = 0.0f;
  envelopeLeft_ = 0.0f;
  envelopeRight_ = 0.0f;
  gain_ = 1.0f;
}

float CompressorProcessor::detectorLevel(float input, float& previousInput, float& previousOutput, float& envelope) const
{
  const float highPassed = input - previousInput + sidechainHpfCoefficient_ * previousOutput;
  previousInput = input;
  previousOutput = highPassed;
  const float detected = detector_ == Detector::Peak ? std::fabs(highPassed) : highPassed * highPassed;
  const float coefficient = detected > envelope ? attackCoefficient_ : releaseCoefficient_;
  envelope = coefficient * envelope + (1.0f - coefficient) * detected;
  return detector_ == Detector::Peak ? envelope : std::sqrt(std::max(0.0f, envelope));
}

float CompressorProcessor::gainForLevel(float level) const
{
  const float levelDb = 20.0f * std::log10(std::max(level, 1.0e-9f));
  float gainDb = 0.0f;
  if (kneeDb_ <= 0.0f) {
    if (levelDb > thresholdDb_) {
      gainDb = (thresholdDb_ + (levelDb - thresholdDb_) / ratio_) - levelDb;
    }
  } else {
    const float kneeStart = thresholdDb_ - kneeDb_ * 0.5f;
    const float kneeEnd = thresholdDb_ + kneeDb_ * 0.5f;
    if (levelDb >= kneeEnd) {
      gainDb = (thresholdDb_ + (levelDb - thresholdDb_) / ratio_) - levelDb;
    } else if (levelDb > kneeStart) {
      const float distance = levelDb - kneeStart;
      gainDb = (1.0f / ratio_ - 1.0f) * distance * distance / (2.0f * kneeDb_);
    }
  }
  return dbToGain(gainDb);
}

StereoSample CompressorProcessor::process(StereoSample input)
{
  refreshLiveParameters();
  const StereoSample driven{input.left * inputGain_, input.right * inputGain_};
  // Keep the detector filters per channel, then link them with the maximum
  // level. One gain envelope is applied to both channels so a guitar part
  // panned to one side cannot make the stereo image wander under compression.
  const float leftLevel = detectorLevel(driven.left, sidechainPreviousInputLeft_,
                                        sidechainPreviousOutputLeft_, envelopeLeft_);
  const float rightLevel = detectorLevel(driven.right, sidechainPreviousInputRight_,
                                         sidechainPreviousOutputRight_, envelopeRight_);
  const float targetGain = gainForLevel(std::max(leftLevel, rightLevel));
  // detectorLevel() already defines the labelled attack/release response.
  // Smoothing gain a second time stretches both timings and makes the UI
  // contract inaccurate. The detector remains stereo-linked below; this
  // stage is only the static gain computer and application.
  gain_ = targetGain;

  const StereoSample wet{driven.left * gain_ * makeupGain_, driven.right * gain_ * makeupGain_};
  return {
    input.left * (1.0f - mix_) + wet.left * mix_,
    input.right * (1.0f - mix_) + wet.right * mix_,
  };
}

} // namespace ardor
