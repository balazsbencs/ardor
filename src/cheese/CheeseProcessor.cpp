#include "cheese/CheeseProcessor.h"

#include "cheese/CheeseNetlist.h"

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

bool CheeseProcessor::configure(const nlohmann::json& params, float sampleRate, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "big cheese sample rate must be finite and positive";
    return false;
  }
  const auto mode = params.value("mode", std::string{"big_cheese"});
  if (mode != "big_cheese") {
    error = "unsupported fuzz mode: " + mode;
    return false;
  }
  if (!cheeseNetlistValid(cheeseNetlist())) {
    error = "big cheese netlist holds a component value that is not realizable";
    return false;
  }

  sampleRate_ = sampleRate;
  fuzzTarget_ = configuredNumber(params, "fuzz", 0.7f);
  toneTarget_ = configuredNumber(params, "tone", 0.5f);
  volumeTarget_ = configuredNumber(params, "volume", 0.7f);

  constexpr float kSmoothingSeconds = 0.015f;
  smoothing_ = 1.0f - std::exp(-1.0f / (kSmoothingSeconds * sampleRate_));

  circuit_.init(cheeseNetlist(), sampleRate_ * 8.0f);
  reset();
  return true;
}

bool CheeseProcessor::setParameterTarget(const std::string& key, float value)
{
  if (!std::isfinite(value)) return false;
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  if (key == "fuzz") fuzzTarget_ = clamped;
  else if (key == "tone") toneTarget_ = clamped;
  else if (key == "volume") volumeTarget_ = clamped;
  else return false;
  return true;
}

void CheeseProcessor::reset()
{
  up2x_.Reset();
  up4x_.Reset();
  up8x_.Reset();
  down4x_.Reset();
  down2x_.Reset();
  down1x_.Reset();
  fuzz_ = fuzzTarget_;
  tone_ = toneTarget_;
  volume_ = volumeTarget_;
  appliedFuzz_ = fuzz_;
  appliedTone_ = tone_;
  circuit_.setControls(fuzz_, tone_, volume_);
  circuit_.reset();
}

StereoSample CheeseProcessor::process(StereoSample input)
{
  fuzz_ += smoothing_ * (fuzzTarget_ - fuzz_);
  tone_ += smoothing_ * (toneTarget_ - tone_);
  volume_ += smoothing_ * (volumeTarget_ - volume_);

  // Volume is a plain output gain and costs nothing, so it follows the smoother
  // every sample. Fuzz and Tone change the matrices, so they only get pushed
  // through once they have moved a step.
  const bool rebuild = std::fabs(fuzz_ - appliedFuzz_) >= kRebuildStep
    || std::fabs(tone_ - appliedTone_) >= kRebuildStep;
  if (rebuild) {
    appliedFuzz_ = fuzz_;
    appliedTone_ = tone_;
  }
  circuit_.setControls(appliedFuzz_, appliedTone_, volume_);

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
