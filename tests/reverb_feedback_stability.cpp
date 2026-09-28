#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/DenormalGuard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {

constexpr int kSampleRate = 48000;

struct TailMetrics {
  double peak = 0.0;
  double middleEnergy = 0.0;
  double lateEnergy = 0.0;
};

TailMetrics render(const std::string& mode, float feedback, int seconds)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("reverb", mode);
  if (!descriptor) throw std::runtime_error("missing reverb descriptor: " + mode);
  auto params = ardor::defaultDaisyFxParams(*descriptor);
  params["mix"] = 1.0f;
  params["pre_delay"] = 0.0f;
  params["tone"] = 0.5f;
  params["decay"] = 1.0f;
  params["mod"] = mode == "shimmer" ? feedback : 0.0f;
  if (mode == "bloom") {
    params["param1"] = 0.5f;
    params["param2"] = feedback;
  } else {
    // Near-unison pitch exposes the extra-loop instability most clearly.
    params["param1"] = 1.0f / 3.0f;
    params["param2"] = 1.0f / 3.0f;
  }

  ardor::DaisyFxProcessor processor;
  std::string error;
  if (!processor.configure("reverb", params, float(kSampleRate), error)) {
    throw std::runtime_error(error);
  }

  TailMetrics metrics;
  std::uint32_t random = 1;
  for (int n = 0; n < seconds * kSampleRate; ++n) {
    random = random * 1664525U + 1013904223U;
    float input = 0.0f;
    if (n < 2 * kSampleRate && n % (kSampleRate / 2) < kSampleRate / 10) {
      input = 0.25f * (0.65f * std::sin(6.28318530718f * 173.0f * n / kSampleRate)
                     + 0.35f * (float(std::int32_t(random)) / 2147483648.0f));
    }
    const auto output = processor.process({input, 0.8f * input});
    const double peak = std::max(std::fabs(output.left), std::fabs(output.right));
    if (!std::isfinite(peak)) throw std::runtime_error(mode + " produced non-finite output");
    metrics.peak = std::max(metrics.peak, peak);
    const double energy = double(output.left) * output.left + double(output.right) * output.right;
    if (n >= 20 * kSampleRate && n < 30 * kSampleRate) metrics.middleEnergy += energy;
    if (n >= 60 * kSampleRate && n < 70 * kSampleRate) metrics.lateEnergy += energy;
  }
  return metrics;
}

} // namespace

int main()
{
  ardor::ScopedDenormalGuard guard;
  const auto bloomLow = render("bloom", 0.0f, 80);
  const auto bloomHigh = render("bloom", 1.0f, 80);
  const auto shimmerHigh = render("shimmer", 1.0f, 90);
  if (bloomHigh.peak > 2.0 || shimmerHigh.peak > 2.0) {
    throw std::runtime_error("long reverb feedback grew above the bounded input response");
  }
  if (bloomHigh.middleEnergy < bloomLow.middleEnergy * 1.2) {
    throw std::runtime_error("Bloom Feedback did not increase late sustain");
  }
  if (bloomHigh.lateEnergy >= bloomHigh.middleEnergy * 0.01
      || shimmerHigh.lateEnergy >= shimmerHigh.middleEnergy * 0.01) {
    throw std::runtime_error("long reverb feedback failed to decay after the input stopped");
  }
}
