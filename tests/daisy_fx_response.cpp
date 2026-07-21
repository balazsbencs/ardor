#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

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

void renderEndpoint(const ardor::DaisyFxDescriptor& descriptor, float endpoint)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  for (const auto& param : descriptor.params) {
    params[param.key] = endpoint;
  }

  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure(descriptor.blockType, params, 48000.0f, error),
          descriptor.mode + ": " + error);

  constexpr int kFrames = 3 * 48000;
  float peak = 0.0f;
  for (int i = 0; i < kFrames; ++i) {
    // Continuous anti-phase stimulus exercises the stereo paths and gives
    // envelope-driven effects a realistic trigger rather than a one-sample
    // impulse. It remains comfortably below full scale at the input.
    const float input = 0.45f * std::sin(6.28318530718f * 219.0f * static_cast<float>(i) / 48000.0f);
    const auto output = processor.process({input, -input});
    require(std::isfinite(output.left) && std::isfinite(output.right),
            descriptor.mode + " endpoint render must remain finite");
    peak = std::fmax(peak, std::fabs(output.left));
    peak = std::fmax(peak, std::fabs(output.right));
  }
  // Hosted effects do not impose a final limiter, so this deliberately leaves
  // several dB of headroom while still detecting a numerically runaway loop.
  require(peak < 4.0f, descriptor.mode + " endpoint render must remain bounded");
}

} // namespace

int main()
{
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    renderEndpoint(descriptor, 0.0f);
    renderEndpoint(descriptor, 1.0f);
  }
}
