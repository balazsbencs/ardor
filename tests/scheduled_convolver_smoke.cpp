#include "dsp/ScheduledConvolver.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

// Direct time-domain convolution: the ground truth the scheduled engine must
// reproduce. Slow, but this is a test.
std::vector<float> directConvolve(const std::vector<float>& input,
                                  const std::vector<float>& impulse)
{
  std::vector<float> out(input.size(), 0.0f);
  for (std::size_t n = 0; n < input.size(); ++n) {
    float sum = 0.0f;
    const std::size_t taps = std::min(impulse.size(), n + 1);
    for (std::size_t k = 0; k < taps; ++k) {
      sum += impulse[k] * input[n - k];
    }
    out[n] = sum;
  }
  return out;
}

std::vector<float> noise(std::size_t frames, unsigned seed, float decay = 0.0f)
{
  std::vector<float> out(frames);
  unsigned state = seed;
  for (std::size_t i = 0; i < frames; ++i) {
    state = state * 1664525u + 1013904223u;
    const float v = static_cast<float>(static_cast<int>(state)) * (1.0f / 2147483648.0f);
    out[i] = decay > 0.0f
        ? v * std::exp(-decay * static_cast<float>(i) / static_cast<float>(frames))
        : v;
  }
  return out;
}

// The whole point: the spread schedule must produce exactly what a direct
// convolution produces, offset by one partition.
void verifyMatchesDirectConvolution(std::size_t impulseFrames, std::size_t partition)
{
  const auto impulse = noise(impulseFrames, 17, 5.0f);
  const auto input = noise(impulseFrames * 3, 91);
  const auto expected = directConvolve(input, impulse);

  ardor::ScheduledConvolver convolver;
  convolver.load(impulse, partition);

  std::vector<float> actual;
  actual.reserve(input.size());
  for (const float x : input) actual.push_back(convolver.process(x));

  const std::size_t latency = convolver.partitionFrames();
  double worst = 0.0;
  double reference = 0.0;
  for (std::size_t i = 0; i + latency < actual.size(); ++i) {
    worst = std::max(worst, static_cast<double>(std::fabs(actual[i + latency] - expected[i])));
    reference = std::max(reference, static_cast<double>(std::fabs(expected[i])));
  }
  const double relative = worst / (reference + 1.0e-12);
  require(relative < 1.0e-4,
          "scheduled convolution must match direct convolution for impulse " +
              std::to_string(impulseFrames) + " partition " + std::to_string(partition) +
              "; relative error " + std::to_string(relative));
}

// Nothing may emerge before the reported latency.
void verifyLatencyIsExact()
{
  const auto impulse = noise(2048, 5, 3.0f);
  ardor::ScheduledConvolver convolver;
  convolver.load(impulse, 512);

  const std::size_t latency = convolver.partitionFrames();
  for (std::size_t i = 0; i < latency; ++i) {
    const float out = convolver.process(i == 0 ? 1.0f : 0.0f);
    require(std::fabs(out) < 1.0e-9f, "no output may precede the reported latency");
  }
  const float first = convolver.process(0.0f);
  require(std::fabs(first - impulse[0]) < 1.0e-4f,
          "the first sample after the latency must be the impulse's first tap");
}

// An impulse with more partitions than the period has samples still has to
// finish, via the settle path at the boundary.
void verifyMorePartitionsThanPeriodSamples()
{
  verifyMatchesDirectConvolution(4096, 16);
}

void verifyResetClearsState()
{
  const auto impulse = noise(1024, 23, 4.0f);
  ardor::ScheduledConvolver convolver;
  convolver.load(impulse, 256);
  for (int i = 0; i < 4096; ++i) convolver.process(1.0f);

  convolver.reset();
  for (std::size_t i = 0; i < convolver.partitionFrames(); ++i) {
    require(std::fabs(convolver.process(0.0f)) < 1.0e-9f,
            "reset must leave no residue behind");
  }
}

void verifyEmptyImpulsePassesThrough()
{
  ardor::ScheduledConvolver convolver;
  convolver.load({}, 256);
  require(!convolver.loaded(), "an empty impulse must report as unloaded");
  require(convolver.process(0.75f) == 0.75f, "an unloaded convolver must pass audio through");
}

} // namespace

int main()
{
  // Cover partitions both larger and smaller than the impulse, and a partition
  // count that does not divide the impulse evenly.
  verifyMatchesDirectConvolution(4096, 1024);
  verifyMatchesDirectConvolution(4096, 256);
  verifyMatchesDirectConvolution(3000, 512);
  verifyMatchesDirectConvolution(300, 1024);
  verifyMorePartitionsThanPeriodSamples();
  verifyLatencyIsExact();
  verifyResetClearsState();
  verifyEmptyImpulsePassesThrough();
  std::printf("scheduled convolver smoke passed\n");
  return 0;
}
