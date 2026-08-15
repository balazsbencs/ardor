#include "rat/RatProcessor.h"

#include "rat/RatNetlist.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

float configuredNumber(const nlohmann::json& params, const char* key, float fallback)
{
  if (!params.is_object()) return fallback;
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

} // namespace

bool RatProcessor::configure(const nlohmann::json& params, float sampleRate, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "rat sample rate must be finite and positive";
    return false;
  }
  const auto mode = params.value("mode", std::string{"rat"});
  if (mode != "rat") {
    error = "unsupported distortion mode: " + mode;
    return false;
  }
  if (!ratNetlistValid(ratNetlist())) {
    error = "rat netlist holds a component value that is not realizable";
    return false;
  }

  sampleRate_ = sampleRate;
  distortionTarget_ = configuredNumber(params, "distortion", 0.5f);
  filterTarget_ = configuredNumber(params, "filter", 0.5f);
  volumeTarget_ = configuredNumber(params, "volume", 0.7f);

  // A knob turn has to reach the circuit without stepping, and the circuit's
  // coefficients are recomputed from scratch whenever a control moves.
  constexpr float kSmoothingSeconds = 0.015f;
  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * sampleRate_));

  circuit_.init(ratNetlist(), sampleRate_ * 8.0f);
  reset();
  return true;
}

bool RatProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!std::isfinite(value)) return false;
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  if (key == "distortion") distortionTarget_ = clamped;
  else if (key == "filter") filterTarget_ = clamped;
  else if (key == "volume") volumeTarget_ = clamped;
  else return false;
  return true;
}

void RatProcessor::reset()
{
  up2x_.Reset();
  up4x_.Reset();
  up8x_.Reset();
  down4x_.Reset();
  down2x_.Reset();
  down1x_.Reset();
  circuit_.reset();
  distortion_ = distortionTarget_;
  filter_ = filterTarget_;
  volume_ = volumeTarget_;
  circuit_.setControls(distortion_, filter_, volume_);
}

StereoSample RatProcessor::process(StereoSample input)
{
  distortion_ += smoothing_ * (distortionTarget_ - distortion_);
  filter_ += smoothing_ * (filterTarget_ - filter_);
  volume_ += smoothing_ * (volumeTarget_ - volume_);
  circuit_.setControls(distortion_, filter_, volume_);

  const float mono = (input.left + input.right) * 0.5f;
  const auto at2x = up2x_.Process(mono);
  float at2xFiltered[2]{};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto at4x = up4x_.Process(at2x[i]);
    for (const float quarterRate : at4x) {
      const auto at8x = up8x_.Process(quarterRate);
      float at4xFiltered = 0.0f;
      for (const float sample : at8x) {
        const float processed = circuit_.process(sample);
        float decimated = 0.0f;
        if (down4x_.Push(processed, decimated)) at4xFiltered = decimated;
      }
      float decimated = 0.0f;
      if (down2x_.Push(at4xFiltered, decimated)) at2xFiltered[i] = decimated;
    }
  }
  float output = 0.0f;
  for (const float sample : at2xFiltered) (void)down1x_.Push(sample, output);
  return {output, output};
}

} // namespace ardor
