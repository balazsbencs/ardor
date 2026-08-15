#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <nlohmann/json.hpp>

#include <string>

namespace ardor {

// Independent attack and sustain shaping, in the style of an SPL Transient
// Designer.
//
// The compressor already in this chain controls level against a threshold: how
// loud a note is allowed to get. This controls shape instead — how much of the
// pick attack survives, and how long the note hangs on afterwards — and it does
// so without a threshold, so it behaves the same on a quiet passage as on a
// loud one.
//
// It measures shape by comparing the level against a smoothed copy of itself:
//
//   Attack   the level minus a 20 ms average of the level. Positive only while
//            the level climbs away from where it recently sat, which is the
//            pick transient.
//
//   Sustain  a 200 ms average of the level minus the level. Positive while the
//            note falls below where it recently sat, which is the decay.
//
// Comparing against a smoothed copy of the level, rather than against a second
// follower with a different time constant, matters. The average of a steady
// level equals that level, so both measurements are exactly zero on held
// material and neither responds to the ripple of the waveform itself. Two
// followers with different time constants sit at different heights on a steady
// tone, and that offset would shape every note whether or not it had a
// transient.
//
// Both measurements are differences of dB, so they describe the shape of the
// envelope and not its height, and one setting works across a performance.
class TransientShaperProcessor {
public:
  TransientShaperProcessor() = default;
  TransientShaperProcessor(TransientShaperProcessor&&) noexcept = default;
  TransientShaperProcessor& operator=(TransientShaperProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);

  // Gain currently applied, in dB. Published for metering.
  float currentGainDb() const noexcept { return lastGainDb_; }

private:
  TransientShaperProcessor(const TransientShaperProcessor&) = delete;
  TransientShaperProcessor& operator=(const TransientShaperProcessor&) = delete;

  static constexpr float kSilenceDb = -140.0f;

  float sampleRate_ = 48000.0f;

  // -1 .. +1. Negative softens, positive sharpens.
  float attackAmount_ = 0.0f;
  float sustainAmount_ = 0.0f;
  float outputGain_ = 1.0f;
  float mix_ = 1.0f;

  // One-pole coefficients, all derived from the sample rate in configure().
  float levelAttack_ = 1.0f;
  float levelRelease_ = 1.0f;
  float attackReference_ = 1.0f;
  float sustainReference_ = 1.0f;
  float gainSmoothing_ = 1.0f;

  float level_ = 0.0f;
  float attackReferenceDb_ = kSilenceDb;
  float sustainReferenceDb_ = kSilenceDb;
  float smoothedGain_ = 1.0f;
  float lastGainDb_ = 0.0f;
};

} // namespace ardor
