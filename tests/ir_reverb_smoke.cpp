#include "dsp/IrReverbProcessor.h"

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

constexpr float kRate = 48000.0f;

// Renders `frames` samples, feeding a single unit impulse at sample 0.
std::vector<ardor::StereoSample> renderImpulse(ardor::IrReverbProcessor& reverb, int frames)
{
  std::vector<ardor::StereoSample> out;
  out.reserve(static_cast<std::size_t>(frames));
  for (int n = 0; n < frames; ++n) {
    const float x = n == 0 ? 1.0f : 0.0f;
    out.push_back(reverb.process({x, x}));
  }
  return out;
}

// An impulse response with a known shape: a delta at `delay` and an
// exponentially decaying noise tail.
std::vector<float> syntheticIr(int frames, int delay, unsigned seed)
{
  std::vector<float> ir(static_cast<std::size_t>(frames), 0.0f);
  unsigned state = seed;
  for (int i = delay; i < frames; ++i) {
    state = state * 1664525u + 1013904223u;
    const float noise = static_cast<float>(static_cast<int>(state)) * (1.0f / 2147483648.0f);
    ir[static_cast<std::size_t>(i)] =
        noise * std::exp(-4.0f * static_cast<float>(i - delay) / static_cast<float>(frames));
  }
  ir[static_cast<std::size_t>(delay)] = 1.0f;
  return ir;
}

// A dry-only setting must pass audio through untouched.
void verifyDryPassthrough()
{
  ardor::IrReverbProcessor reverb;
  std::string error;
  require(reverb.load(syntheticIr(4800, 0, 7), {}, kRate, error), error);
  reverb.setMix(0.0f);
  reverb.setLevelDb(0.0f);
  // reset() snaps the smoothed mix and level to their targets, which is what a
  // preset load does. Without it the block ramps over ~42 ms from its previous
  // setting, which is correct for a live knob move but not for a fresh load.
  reverb.reset();

  for (int n = 0; n < 4096; ++n) {
    const float x = std::sin(6.2831853f * 220.0f * static_cast<float>(n) / kRate);
    const auto out = reverb.process({x, x});
    require(std::fabs(out.left - x) < 0.02f, "dry path must pass through at mix 0");
  }
}

// The wet path must reproduce the impulse response, offset by the partition the
// convolver buffers into.
void verifyImpulseResponseAlignment()
{
  constexpr int kIrFrames = 8192;
  const auto ir = syntheticIr(kIrFrames, 0, 11);

  ardor::IrReverbProcessor reverb;
  std::string error;
  require(reverb.load(ir, {}, kRate, error), error);
  reverb.setMix(1.0f);
  reverb.setLevelDb(0.0f);
  reverb.setPreDelayMs(0.0f);
  // Keep the wet filters out of the way so this compares the raw convolution.
  reverb.setLowCutHz(20.0f);
  reverb.setHighCutHz(20000.0f);
  reverb.reset();

  const int latency = static_cast<int>(reverb.preDelayFrames());
  const auto rendered = renderImpulse(reverb, latency + kIrFrames);

  // The wet output, shifted back by the reported latency, must be the impulse.
  double worst = 0.0;
  for (int i = 0; i < kIrFrames; ++i) {
    const float expected = ir[static_cast<std::size_t>(i)];
    const float actual = rendered[static_cast<std::size_t>(latency + i)].left;
    worst = std::max(worst, static_cast<double>(std::fabs(actual - expected)));
  }
  require(worst < 0.01,
          "wet path must reproduce the impulse response; worst error " + std::to_string(worst));

  // Nothing may arrive before the reported latency.
  double early = 0.0;
  for (int i = 0; i < latency; ++i) {
    early = std::max(early, static_cast<double>(std::fabs(rendered[static_cast<std::size_t>(i)].left)));
  }
  require(early < 1.0e-6, "no wet output may arrive before the reported latency");
}

// Pre-delay must push the tail later by the amount asked for.
void verifyPreDelayShiftsTheTail()
{
  constexpr int kIrFrames = 4096;
  const auto ir = syntheticIr(kIrFrames, 0, 3);
  const float preDelayMs = 50.0f;
  const int expectedShift = static_cast<int>(preDelayMs * 0.001f * kRate);

  const auto firstArrival = [&](float milliseconds) {
    ardor::IrReverbProcessor reverb;
    std::string error;
    require(reverb.load(ir, {}, kRate, error), error);
    reverb.setMix(1.0f);
    reverb.setPreDelayMs(milliseconds);
    reverb.reset();
    const auto rendered = renderImpulse(reverb, 16384);
    for (std::size_t i = 0; i < rendered.size(); ++i) {
      if (std::fabs(rendered[i].left) > 0.05f) return static_cast<int>(i);
    }
    return -1;
  };

  const int without = firstArrival(0.0f);
  const int with = firstArrival(preDelayMs);
  require(without >= 0 && with >= 0, "the wet tail must arrive in both cases");
  const int measured = with - without;
  require(std::abs(measured - expectedShift) <= 2,
          "pre-delay must shift the tail by the requested amount; got " +
              std::to_string(measured) + " expected " + std::to_string(expectedShift));
}

// A stereo impulse must drive the two channels independently.
void verifyStereoImpulsesStayIndependent()
{
  ardor::IrReverbProcessor reverb;
  std::string error;
  require(reverb.load(syntheticIr(4096, 0, 5), syntheticIr(4096, 0, 9), kRate, error), error);
  reverb.setMix(1.0f);
  reverb.reset();

  const auto rendered = renderImpulse(reverb, 8192);
  double difference = 0.0;
  for (const auto& sample : rendered) {
    difference += std::fabs(static_cast<double>(sample.left) - sample.right);
  }
  require(difference > 1.0, "distinct left and right impulses must produce distinct channels");
}

// Long impulses are truncated rather than allowed to consume unbounded memory.
void verifyImpulseLengthIsCapped()
{
  const int overLong = static_cast<int>((ardor::IrReverbProcessor::MAX_IMPULSE_SECONDS + 2.0f) * kRate);
  ardor::IrReverbProcessor reverb;
  std::string error;
  require(reverb.load(syntheticIr(overLong, 0, 13), {}, kRate, error), error);
  const std::size_t cap =
      static_cast<std::size_t>(ardor::IrReverbProcessor::MAX_IMPULSE_SECONDS * kRate);
  require(reverb.tailFrames() <= cap + ardor::IrReverbProcessor::PARTITION_FRAMES,
          "an over-long impulse must be truncated to the documented cap");
}

void verifyRejectsBadInput()
{
  ardor::IrReverbProcessor reverb;
  std::string error;
  require(!reverb.load({}, {}, kRate, error), "an empty impulse must be rejected");
  require(!error.empty(), "rejection must explain itself");
  require(!reverb.load(syntheticIr(128, 0, 1), {}, 0.0f, error),
          "a non-positive sample rate must be rejected");

  // Unloaded, the block must be a clean bypass rather than silence.
  ardor::IrReverbProcessor empty;
  const auto out = empty.process({0.5f, -0.25f});
  require(out.left == 0.5f && out.right == -0.25f,
          "an unloaded convolution reverb must pass audio through");
}

} // namespace

int main()
{
  verifyDryPassthrough();
  verifyImpulseResponseAlignment();
  verifyPreDelayShiftsTheTail();
  verifyStereoImpulsesStayIndependent();
  verifyImpulseLengthIsCapped();
  verifyRejectsBadInput();
  std::printf("ir reverb smoke passed\n");
  return 0;
}
