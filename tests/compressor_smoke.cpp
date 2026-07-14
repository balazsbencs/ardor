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

  compressor.reset();
  const float firstAfterReset = compressor.process({1.0f, 1.0f}).left;
  compressor.reset();
  require(firstAfterReset == compressor.process({1.0f, 1.0f}).left, "reset deterministic");

  nlohmann::json invalid = settings;
  invalid["detector"] = "unknown";
  require(!compressor.configure(invalid, 48000.0f, error), "invalid detector rejected");
}
