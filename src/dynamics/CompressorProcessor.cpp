#include "dynamics/CompressorProcessor.h"

#include <algorithm>
#include <cmath>

namespace ardor {

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
  reset();
  return true;
}

void CompressorProcessor::reset()
{
  sidechainPreviousInputLeft_ = 0.0f;
  sidechainPreviousInputRight_ = 0.0f;
  sidechainPreviousOutputLeft_ = 0.0f;
  sidechainPreviousOutputRight_ = 0.0f;
  envelopeLeft_ = 0.0f;
  envelopeRight_ = 0.0f;
  gainLeft_ = 1.0f;
  gainRight_ = 1.0f;
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
  const StereoSample driven{input.left * inputGain_, input.right * inputGain_};
  const float targetGainLeft = gainForLevel(detectorLevel(driven.left, sidechainPreviousInputLeft_,
                                                          sidechainPreviousOutputLeft_, envelopeLeft_));
  const float targetGainRight = gainForLevel(detectorLevel(driven.right, sidechainPreviousInputRight_,
                                                           sidechainPreviousOutputRight_, envelopeRight_));
  const float gainCoefficientLeft = targetGainLeft < gainLeft_ ? attackCoefficient_ : releaseCoefficient_;
  const float gainCoefficientRight = targetGainRight < gainRight_ ? attackCoefficient_ : releaseCoefficient_;
  gainLeft_ = gainCoefficientLeft * gainLeft_ + (1.0f - gainCoefficientLeft) * targetGainLeft;
  gainRight_ = gainCoefficientRight * gainRight_ + (1.0f - gainCoefficientRight) * targetGainRight;

  const StereoSample wet{driven.left * gainLeft_ * makeupGain_, driven.right * gainRight_ * makeupGain_};
  return {
    input.left * (1.0f - mix_) + wet.left * mix_,
    input.right * (1.0f - mix_) + wet.right * mix_,
  };
}

} // namespace ardor
