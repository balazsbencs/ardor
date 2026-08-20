#include "dynamics/TransientShaperProcessor.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

// How many dB of change a full attack or sustain setting can apply. Deep enough
// to reshape a note, shallow enough that the control stays usable across its
// whole travel.
constexpr float kMaxShapeDb = 12.0f;
// Envelope departure, in dB, that counts as full strength. Beyond it the effect
// stops increasing, so a hard pick does not get a wildly different amount of
// shaping from a soft one. The two differ by an order of magnitude because the
// gestures do: a pick onset jumps tens of dB in milliseconds, while a decay
// gives up only a few dB over the whole sustain window.
constexpr float kFullScaleAttackDb = 10.0f;
constexpr float kFullScaleSustainDb = 3.0f;

// The level detector. Its release has to be long compared with the period of
// the lowest note, or the level would ripple at the waveform frequency and the
// measurements would follow that ripple instead of the envelope.
constexpr float kLevelAttackMs = 0.5f;
constexpr float kLevelReleaseMs = 120.0f;
// What "recently" means for each measurement. The attack window covers a pick;
// the sustain window covers the body of a note.
constexpr float kAttackReferenceMs = 20.0f;
constexpr float kSustainReferenceMs = 200.0f;
// The gain smoother has to settle inside the transient it is shaping. A slow
// one arrives after the pick has already passed.
constexpr float kGainSmoothingMs = 2.0f;

// Below this the input is noise, and boosting its decay would just raise the
// noise floor. The effect fades out over the range above it.
constexpr float kNoiseFloorDb = -70.0f;
constexpr float kNoiseFadeDb = 15.0f;

float readNumber(const nlohmann::json& params, const char* key, float fallback)
{
  if (!params.is_object()) return fallback;
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? value : fallback;
}

float coefficientFor(float milliseconds, float sampleRate)
{
  if (!(milliseconds > 0.0f)) return 1.0f;
  return std::clamp(1.0f - std::exp(-1000.0f / (milliseconds * sampleRate)), 0.0f, 1.0f);
}

float linearToDb(float value)
{
  return 20.0f * std::log10(std::max(value, 1.0e-7f));
}

} // namespace

bool TransientShaperProcessor::configure(const nlohmann::json& params, float sampleRate,
                                         std::string& error)
{
  if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate)) {
    error = "transient shaper needs a positive sample rate";
    return false;
  }
  sampleRate_ = sampleRate;

  levelAttack_ = coefficientFor(kLevelAttackMs, sampleRate_);
  levelRelease_ = coefficientFor(kLevelReleaseMs, sampleRate_);
  attackReference_ = coefficientFor(kAttackReferenceMs, sampleRate_);
  sustainReference_ = coefficientFor(kSustainReferenceMs, sampleRate_);
  gainSmoothing_ = coefficientFor(kGainSmoothingMs, sampleRate_);

  setParameterTarget("attack", readNumber(params, "attack", 0.0f));
  setParameterTarget("sustain", readNumber(params, "sustain", 0.0f));
  setParameterTarget("output_db", readNumber(params, "output_db", 0.0f));
  setParameterTarget("mix", readNumber(params, "mix", 1.0f));

  reset();
  error.clear();
  return true;
}

bool TransientShaperProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!std::isfinite(value)) return false;
  if (key == "attack") {
    attackAmount_ = std::clamp(value, -100.0f, 100.0f) * 0.01f;
    return true;
  }
  if (key == "sustain") {
    sustainAmount_ = std::clamp(value, -100.0f, 100.0f) * 0.01f;
    return true;
  }
  if (key == "output_db") {
    outputGain_ = std::pow(10.0f, std::clamp(value, -24.0f, 24.0f) / 20.0f);
    return true;
  }
  if (key == "mix") {
    mix_ = std::clamp(value, 0.0f, 1.0f);
    return true;
  }
  return false;
}

void TransientShaperProcessor::reset()
{
  level_ = 0.0f;
  attackReferenceDb_ = kSilenceDb;
  sustainReferenceDb_ = kSilenceDb;
  smoothedGain_ = 1.0f;
  lastGainDb_ = 0.0f;
}

StereoSample TransientShaperProcessor::process(StereoSample input)
{
  const float left = std::isfinite(input.left) ? input.left : 0.0f;
  const float right = std::isfinite(input.right) ? input.right : 0.0f;

  // One detector across both channels, so the image cannot wander when only one
  // side carries a transient.
  const float rectified = std::max(std::fabs(left), std::fabs(right));
  const float coefficient = rectified > level_ ? levelAttack_ : levelRelease_;
  level_ += coefficient * (rectified - level_);

  const float levelDb = linearToDb(level_);
  attackReferenceDb_ += attackReference_ * (levelDb - attackReferenceDb_);
  sustainReferenceDb_ += sustainReference_ * (levelDb - sustainReferenceDb_);

  // Positive only while the level climbs away from where it recently sat.
  const float attackDepartureDb = std::max(0.0f, levelDb - attackReferenceDb_);
  // Positive only while it falls below where it recently sat.
  const float sustainDepartureDb = std::max(0.0f, sustainReferenceDb_ - levelDb);

  const float audible = std::clamp((levelDb - kNoiseFloorDb) / kNoiseFadeDb, 0.0f, 1.0f);
  const float attackWeight =
      audible * std::min(attackDepartureDb / kFullScaleAttackDb, 1.0f);
  const float sustainWeight =
      audible * std::min(sustainDepartureDb / kFullScaleSustainDb, 1.0f);

  const float gainDb = kMaxShapeDb * (attackAmount_ * attackWeight +
                                      sustainAmount_ * sustainWeight);
  const float targetGain = std::pow(10.0f, gainDb / 20.0f);

  smoothedGain_ += gainSmoothing_ * (targetGain - smoothedGain_);
  lastGainDb_ = linearToDb(smoothedGain_);

  const float dry = 1.0f - mix_;
  return StereoSample{
      (left * smoothedGain_ * mix_ + left * dry) * outputGain_,
      (right * smoothedGain_ * mix_ + right * dry) * outputGain_,
  };
}

} // namespace ardor
