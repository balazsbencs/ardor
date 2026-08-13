#include "wah/WahProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ardor {

struct WahProcessor::LiveParameters {
  std::atomic<float> position{0.0f};
  std::atomic<float> levelDb{0.0f};
  std::atomic<std::uint64_t> revision{0};
};

namespace {

float configuredNumber(const nlohmann::json& params, const char* key, float fallback,
                       float minimum, float maximum)
{
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

float dbToLinear(float db)
{
  return std::pow(10.0f, db / 20.0f);
}

} // namespace

bool WahProcessor::configure(const nlohmann::json& params, float sampleRate,
                             const std::filesystem::path& tablePath, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "wah sample rate must be finite and positive";
    return false;
  }
  const auto mode = params.value("mode", std::string{"gcb95"});
  if (mode != "gcb95") {
    error = "unsupported wah mode: " + mode;
    return false;
  }
  if (!circuit_.load(tablePath, error)) return false;
  const float expectedRate = sampleRate * 4.0f;
  if (std::fabs(circuit_.sampleRate() - expectedRate) > 1.0f) {
    error = "wah table sample rate does not match 4x the host sample rate";
    return false;
  }

  sampleRate_ = sampleRate;
  positionTarget_ = configuredNumber(params, "position", 0.0f, 0.0f, 1.0f);
  smoothedPosition_ = positionTarget_;
  const float levelDb = configuredNumber(params, "level", 0.0f, -24.0f, 24.0f);
  levelGain_ = dbToLinear(levelDb);
  constexpr float kPositionSmoothingSeconds = 0.015f;
  positionSmoothing_ = 1.0f - std::exp(-1.0f / (kPositionSmoothingSeconds * sampleRate_));

  liveParameters_ = std::make_shared<LiveParameters>();
  liveParameters_->position.store(positionTarget_, std::memory_order_relaxed);
  liveParameters_->levelDb.store(levelDb, std::memory_order_relaxed);
  liveRevision_ = 1;
  liveParameters_->revision.store(liveRevision_, std::memory_order_release);
  reset();
  return true;
}

bool WahProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!liveParameters_ || !std::isfinite(value)) return false;
  if (key == "position") {
    liveParameters_->position.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
  } else if (key == "level") {
    liveParameters_->levelDb.store(std::clamp(value, -24.0f, 24.0f), std::memory_order_relaxed);
  } else {
    return false;
  }
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
  return true;
}

void WahProcessor::refreshLiveParameters()
{
  if (!liveParameters_) return;
  const auto revision = liveParameters_->revision.load(std::memory_order_acquire);
  if (revision == liveRevision_) return;
  liveRevision_ = revision;
  positionTarget_ = liveParameters_->position.load(std::memory_order_relaxed);
  levelGain_ = dbToLinear(liveParameters_->levelDb.load(std::memory_order_relaxed));
}

void WahProcessor::reset()
{
  up2x_.Reset();
  up4x_.Reset();
  down2x_.Reset();
  down1x_.Reset();
  circuit_.reset();
  smoothedPosition_ = positionTarget_;
  circuit_.setPotPosition(smoothedPosition_);
}

StereoSample WahProcessor::process(StereoSample input)
{
  refreshLiveParameters();
  smoothedPosition_ += positionSmoothing_ * (positionTarget_ - smoothedPosition_);
  circuit_.setPotPosition(smoothedPosition_);

  const float mono = (input.left + input.right) * 0.5f;
  const auto at2x = up2x_.Process(mono);
  float at2xFiltered[2]{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto at4x = up4x_.Process(at2x[i]);
    for (const float sample : at4x) {
      const float processed = circuit_.process(sample);
      float decimated = 0.0f;
      if (down2x_.Push(processed, decimated)) at2xFiltered[i] = decimated;
    }
  }
  float output = 0.0f;
  for (const float sample : at2xFiltered) (void)down1x_.Push(sample, output);
  output *= levelGain_;
  return {output, output};
}

} // namespace ardor
