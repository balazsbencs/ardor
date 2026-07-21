#include "dynamics/CompressorProcessor.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

float renderSteady(ardor::CompressorProcessor& compressor, float input, int frames)
{
  ardor::StereoSample sample{};
  for (int i = 0; i < frames; ++i) {
    const float alternating = (i % 2 == 0) ? input : -input;
    sample = compressor.process({alternating, alternating});
  }
  return std::fabs(sample.left);
}

} // namespace

int main()
{
  ardor::CompressorProcessor compressor;
  std::string error;
  const nlohmann::json settings{
    {"threshold_db", -24.0f}, {"ratio", 8.0f}, {"attack_ms", 1.0f},
    {"release_ms", 100.0f}, {"knee_db", 6.0f}, {"makeup_db", 0.0f},
    {"input_gain_db", 0.0f}, {"mix", 1.0f}, {"sidechain_hpf_hz", 80.0f},
    {"detector", "peak"}, {"auto_makeup", false},
  };
  require(compressor.configure(settings, 48000.0f, error), error);

  const float compressed = renderSteady(compressor, 1.0f, 48000);
  require(std::isfinite(compressed), "compressed output finite");
  require(compressed < 0.7f, "compressor reduces above-threshold steady signal");

  ardor::CompressorProcessor stepResponse;
  require(stepResponse.configure(settings, 48000.0f, error), error);
  const float attackStart = std::fabs(stepResponse.process({1.0f, 1.0f}).left);
  const float attackSettled = renderSteady(stepResponse, 1.0f, 480);
  require(attackSettled < attackStart, "attack should reduce gain over its configured interval");
  const float releaseStart = std::fabs(stepResponse.process({0.01f, 0.01f}).left);
  float releaseSettled = releaseStart;
  for (int i = 0; i < 4800; ++i) {
    releaseSettled = std::fabs(stepResponse.process({0.01f, 0.01f}).left);
  }
  require(releaseSettled > releaseStart, "release should restore gain over its configured interval");

  nlohmann::json rmsSettings = settings;
  rmsSettings["detector"] = "rms";
  ardor::CompressorProcessor rmsStepResponse;
  require(rmsStepResponse.configure(rmsSettings, 48000.0f, error), error);
  const float rmsAttackStart = std::fabs(rmsStepResponse.process({1.0f, 1.0f}).left);
  const float rmsAttackSettled = renderSteady(rmsStepResponse, 1.0f, 480);
  require(rmsAttackSettled < rmsAttackStart, "RMS attack should reduce gain over its configured interval");
  const float rmsReleaseStart = std::fabs(rmsStepResponse.process({0.01f, 0.01f}).left);
  float rmsReleaseSettled = rmsReleaseStart;
  for (int i = 0; i < 4800; ++i) {
    rmsReleaseSettled = std::fabs(rmsStepResponse.process({0.01f, 0.01f}).left);
  }
  require(rmsReleaseSettled > rmsReleaseStart, "RMS release should restore gain over its configured interval");
  require(compressor.setParameterTarget("mix", 0.5f), "publish live compressor target");
  require(!compressor.setParameterTarget("detector", 0.0f), "detector is not a live target");

  compressor.reset();
  ardor::StereoSample linked{};
  for (int i = 0; i < 48000; ++i) {
    const float polarity = i % 2 == 0 ? 1.0f : -1.0f;
    linked = compressor.process({polarity, polarity * 0.1f});
  }
  require(std::fabs(std::fabs(linked.left) - std::fabs(linked.right) * 10.0f) < 0.001f,
          "linked compressor must apply the same gain to both channels");

  compressor.reset();
  const float firstAfterReset = compressor.process({1.0f, 1.0f}).left;
  compressor.reset();
  require(firstAfterReset == compressor.process({1.0f, 1.0f}).left, "reset deterministic");

  nlohmann::json invalid = settings;
  invalid["detector"] = "unknown";
  require(!compressor.configure(invalid, 48000.0f, error), "invalid detector rejected");
}
