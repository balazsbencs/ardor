#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/halfband_resampler.h"
#include "wah/WahCircuit.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace ardor {

// Block-facing wrapper for the 192 kHz circuit model. The host signal is
// converted 4x in each direction and the mono circuit output is returned on
// both channels, matching a physical wah in a guitar signal path.
class WahProcessor {
public:
  WahProcessor() = default;
  WahProcessor(WahProcessor&&) noexcept = default;
  WahProcessor& operator=(WahProcessor&&) noexcept = default;

  bool configure(const nlohmann::json& params, float sampleRate,
                 const std::filesystem::path& tablePath, std::string& error);
  bool setParameterTarget(const std::string& key, float value);
  void reset();
  StereoSample process(StereoSample input);
  std::size_t latencyFrames() const noexcept { return 23; }

private:
  WahProcessor(const WahProcessor&) = delete;
  WahProcessor& operator=(const WahProcessor&) = delete;
  struct LiveParameters;

  void refreshLiveParameters();

  WahCircuit circuit_;
  pedal::HalfbandInterpolator2x up2x_;
  pedal::HalfbandInterpolator2x up4x_;
  pedal::HalfbandDecimator2x down2x_;
  pedal::HalfbandDecimator2x down1x_;
  std::shared_ptr<LiveParameters> liveParameters_;
  std::uint64_t liveRevision_ = 0;
  float sampleRate_ = 48000.0f;
  float positionTarget_ = 0.0f;
  float smoothedPosition_ = 0.0f;
  float positionSmoothing_ = 0.0f;
  float levelGain_ = 1.0f;
};

} // namespace ardor
