#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace ardor {

class NoiseGateProcessor {
public:
  NoiseGateProcessor() = default;
  NoiseGateProcessor(NoiseGateProcessor&&) noexcept = default;
  NoiseGateProcessor& operator=(NoiseGateProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);

private:
  NoiseGateProcessor(const NoiseGateProcessor&) = delete;
  NoiseGateProcessor& operator=(const NoiseGateProcessor&) = delete;

  struct LiveParameters;

  float thresholdGain_ = 0.0017782794f;
  float closeThresholdGain_ = 0.0008912509f;
  float reductionGain_ = 0.0001f;
  float attackCoefficient_ = 0.0f;
  float releaseCoefficient_ = 0.0f;
  float sidechainHpfCoefficient_ = 0.0f;
  float sidechainPreviousInputLeft_ = 0.0f;
  float sidechainPreviousInputRight_ = 0.0f;
  float sidechainPreviousOutputLeft_ = 0.0f;
  float sidechainPreviousOutputRight_ = 0.0f;
  float gain_ = 0.0001f;
  float sampleRate_ = 48000.0f;
  std::uint64_t holdSamples_ = 0;
  std::uint64_t holdRemaining_ = 0;
  bool open_ = false;
  std::shared_ptr<LiveParameters> liveParameters_;
  std::uint64_t liveRevision_ = 0;

  void refreshLiveParameters();
  float highPassedLevel(float input, float& previousInput, float& previousOutput) const;
};

} // namespace ardor
