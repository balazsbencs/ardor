#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <nlohmann/json.hpp>

#include <string>
#include <memory>

namespace ardor {

class CompressorProcessor {
public:
  CompressorProcessor() = default;
  CompressorProcessor(CompressorProcessor&&) noexcept = default;
  CompressorProcessor& operator=(CompressorProcessor&&) noexcept = default;
  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);

private:
  CompressorProcessor(const CompressorProcessor&) = delete;
  CompressorProcessor& operator=(const CompressorProcessor&) = delete;
  struct LiveParameters;
  enum class Detector {
    Peak,
    Rms
  };

  float thresholdDb_ = -24.0f;
  float ratio_ = 4.0f;
  float kneeDb_ = 6.0f;
  float attackCoefficient_ = 0.0f;
  float releaseCoefficient_ = 0.0f;
  float inputGain_ = 1.0f;
  float makeupGain_ = 1.0f;
  float mix_ = 1.0f;
  float sidechainHpfCoefficient_ = 0.0f;
  Detector detector_ = Detector::Peak;
  float sidechainPreviousInputLeft_ = 0.0f;
  float sidechainPreviousInputRight_ = 0.0f;
  float sidechainPreviousOutputLeft_ = 0.0f;
  float sidechainPreviousOutputRight_ = 0.0f;
  float envelopeLeft_ = 0.0f;
  float envelopeRight_ = 0.0f;
  float gain_ = 1.0f;
  float sampleRate_ = 48000.0f;
  std::shared_ptr<LiveParameters> liveParameters_;
  uint64_t liveRevision_ = 0;

  void refreshLiveParameters();
  float detectorLevel(float input, float& previousInput, float& previousOutput, float& envelope) const;
  float gainForLevel(float level) const;
};

} // namespace ardor
