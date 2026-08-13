#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kFrames = 12U * 48000U;

double db(double value, double floor = 1e-12)
{
  return 20.0 * std::log10(std::max(value, floor));
}

struct Metrics {
  double peakDb = 0.0;
  double energyDb = 0.0;
  double correlation = 0.0;
  double density100To200 = 0.0;
  double density200To500 = 0.0;
  double rt60From20Db = 0.0;
};

double density(const std::vector<float>& left, const std::vector<float>& right,
               std::size_t begin, std::size_t end, double threshold)
{
  end = std::min(end, left.size());
  if (begin >= end) return 0.0;
  std::size_t active = 0;
  for (std::size_t frame = begin; frame < end; ++frame) {
    if (std::max(std::fabs(left[frame]), std::fabs(right[frame])) > threshold) ++active;
  }
  return static_cast<double>(active) / static_cast<double>(end - begin);
}

Metrics measure(const ardor::DaisyFxDescriptor& descriptor)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  params["mix"] = 1.0f;
  params["pre_delay"] = 0.0f;

  ardor::DaisyFxProcessor processor;
  std::string error;
  if (!processor.configure("reverb", params, kSampleRate, error)) {
    throw std::runtime_error(descriptor.mode + ": " + error);
  }

  std::vector<float> left(kFrames);
  std::vector<float> right(kFrames);
  double peak = 0.0;
  double sumLeft = 0.0;
  double sumRight = 0.0;
  double sumCross = 0.0;
  std::vector<double> frameEnergy(kFrames);
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    const float impulse = frame == 0 ? 1.0f : 0.0f;
    const auto output = processor.process({impulse, impulse});
    if (!std::isfinite(output.left) || !std::isfinite(output.right)) {
      throw std::runtime_error(descriptor.mode + ": non-finite impulse response");
    }
    left[frame] = output.left;
    right[frame] = output.right;
    peak = std::max(peak, static_cast<double>(std::max(std::fabs(output.left), std::fabs(output.right))));
    const double l2 = static_cast<double>(output.left) * output.left;
    const double r2 = static_cast<double>(output.right) * output.right;
    sumLeft += l2;
    sumRight += r2;
    sumCross += static_cast<double>(output.left) * output.right;
    frameEnergy[frame] = l2 + r2;
  }

  Metrics result;
  result.peakDb = db(peak);
  result.energyDb = 10.0 * std::log10(std::max(sumLeft + sumRight, 1e-24));
  result.correlation = sumCross / std::sqrt(std::max(sumLeft * sumRight, 1e-24));
  const double densityThreshold = peak * 0.0001; // -80 dB relative to peak
  result.density100To200 = density(left, right, 4800, 9600, densityThreshold);
  result.density200To500 = density(left, right, 9600, 24000, densityThreshold);

  double remaining = 0.0;
  for (auto it = frameEnergy.rbegin(); it != frameEnergy.rend(); ++it) {
    remaining += *it;
    *it = remaining;
  }
  const double total = frameEnergy.front();
  const double atMinus5 = total * std::pow(10.0, -0.5);
  const double atMinus25 = total * std::pow(10.0, -2.5);
  const auto firstBelow = [&](double target) {
    const auto it = std::find_if(frameEnergy.begin(), frameEnergy.end(),
                                 [target](double value) { return value <= target; });
    return static_cast<std::size_t>(it - frameEnergy.begin());
  };
  const std::size_t t5 = firstBelow(atMinus5);
  const std::size_t t25 = firstBelow(atMinus25);
  result.rt60From20Db = t25 > t5
    ? 3.0 * static_cast<double>(t25 - t5) / kSampleRate : 0.0;
  return result;
}

} // namespace

int main()
{
  std::printf("mode          peak dB  energy dB  L/R corr  density 100-200  density 200-500  RT60(20dB)\n");
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.blockType != "reverb") continue;
    const auto metrics = measure(descriptor);
    std::printf("%-12s %8.2f %10.2f %9.3f %16.3f %16.3f %11.3f s\n",
                descriptor.mode.c_str(), metrics.peakDb, metrics.energyDb,
                metrics.correlation, metrics.density100To200,
                metrics.density200To500, metrics.rt60From20Db);
  }
}
