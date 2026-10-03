#include "dsp/IrReverbProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <limits>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

// Only audio calls enable this guard; kernel preparation on the control
// thread is intentionally allowed to allocate and reclaim memory.
thread_local bool auditRealtimeMemory = false;
thread_local std::size_t realtimeAllocations = 0, realtimeDeallocations = 0;

void* operator new(std::size_t size)
{
  if (auditRealtimeMemory) ++realtimeAllocations;
  if (auto* ptr = std::malloc(std::max<std::size_t>(size, 1))) return ptr;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept
{
  if (auditRealtimeMemory && ptr) ++realtimeDeallocations;
  std::free(ptr);
}
void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { ::operator delete(ptr); }

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
  require(latency == 128, "IR reverb early reflections should have 128-frame latency");
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

void verifyLiveControlsDoNotClick()
{
  for (int control = 0; control < 3; ++control) {
    ardor::IrReverbProcessor reverb;
    std::string error;
    require(reverb.load({1.0f}, {}, kRate, error), error);
    reverb.setMix(1.0f);
    if (control == 2) reverb.setHighCutHz(1000.0f);
    reverb.reset();
    float previous = 0.0f;
    float maximumStep = 0.0f;
    for (int frame = 0; frame < 2 * 48000; ++frame) {
      if (frame == 48000) {
        if (control == 0) reverb.setPreDelayMs(77.0f);
        else reverb.setHighCutHz(control == 1 ? 500.0f : 5000.0f);
      }
      const float input = 0.25f * std::sin(6.28318530718f * 173.0f * frame / kRate);
      const float output = reverb.process({input, input}).left;
      if (frame >= 48000) maximumStep = std::max(maximumStep, std::fabs(output - previous));
      previous = output;
    }
    require(maximumStep < (control == 0 ? 0.03f : 0.02f),
            control == 0 ? "moving IR pre-delay must not click"
                         : "IR high-cut automation must not click");
  }
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

std::vector<float> exponentialIr(float rt60, int onset = 0)
{
  std::vector<float> ir(3 * 48000, 0.0f);
  for (std::size_t i = onset; i < ir.size(); ++i)
    ir[i] = 0.01f * std::exp(-6.90775527898f * (i - onset) / (kRate * rt60));
  return ir;
}

void verifyReverbTimeRatio()
{
  constexpr float rt60 = 1.2f;
  constexpr int onset = 2400;
  const auto original = exponentialIr(rt60, onset);
  auto right = original;
  for (auto& x : right) x *= -0.5f;
  for (float ratio : {1.0f, 0.5f, 0.25f}) {
    ardor::IrReverbProcessor reverb;
    std::string error;
    require(reverb.load(original, right, kRate, error), error);
    require(reverb.originalRt60Seconds().has_value(), "exponential tail has a measurable RT60");
    require(std::fabs(*reverb.originalRt60Seconds() - rt60) < 0.01f,
            "RT60 estimate must match the known exponential decay");
    reverb.setMix(1.0f);
    reverb.setReverbTimeRatio(ratio);
    reverb.reset();
    const auto rendered = renderImpulse(reverb, original.size() + 256);
    const int delay = ardor::IrReverbProcessor::PARTITION_FRAMES;
    for (std::size_t i = 0; i < original.size(); ++i) {
      const float expected = i < onset ? 0.0f
        : 0.01f * std::exp(-6.90775527898f * (i - onset) / (kRate * rt60 * ratio));
      require(std::fabs(rendered[i + delay].left - expected) < 0.000002f,
              "reverb time must change decay without moving reflections or pre-delay");
      require(std::fabs(rendered[i + delay].right + 0.5f * expected) < 0.000002f,
              "one decay envelope must preserve stereo balance and polarity");
    }
    // An independent slope measurement on the output, across the early/tail
    // partition boundary, verifies that the audible decay follows the ratio.
    const float a = rendered[onset + delay + 2400].left;
    const float b = rendered[onset + delay + 4800].left;
    const float measured = -6.90775527898f * 0.05f / std::log(b / a);
    require(std::fabs(measured - rt60 * ratio) < 0.01f,
            "rendered RT60 must track the requested ratio");
  }
}

void verifyUnmeasurableDecayIsUnchanged()
{
  std::string error;
  for (auto ir : {std::vector<float>{1.0f}, std::vector<float>(48000, 0.01f),
                  std::vector<float>(48000, 0.0f)}) {
    ardor::IrReverbProcessor reverb;
    require(reverb.load(ir, {}, kRate, error), error);
    require(!reverb.originalRt60Seconds(), "short, silent, or gated IR has no reliable RT60");
    reverb.setMix(1.0f);
    reverb.setReverbTimeRatio(0.25f);
    reverb.reset();
    const auto rendered = renderImpulse(reverb, ir.size() + 256);
    for (std::size_t i = 0; i < ir.size(); ++i)
      require(std::fabs(rendered[i + ardor::IrReverbProcessor::PARTITION_FRAMES].left - ir[i]) < 0.000002f,
              "unmeasurable impulses must remain unchanged");
  }
}

void verifyLiveDecayPreservesHistory()
{
  const auto ir = exponentialIr(1.2f);
  ardor::IrReverbProcessor live, reference;
  std::string error;
  require(live.load(ir, {}, kRate, error), error);
  require(reference.load(ir, {}, kRate, error), error);
  for (auto* reverb : {&live, &reference}) reverb->setMix(1.0f);
  reference.setReverbTimeRatio(0.5f);
  live.reset(); reference.reset();
  for (int frame = 0; frame < 40000; ++frame) {
    if (frame == 6001) live.setReverbTimeRatio(0.25f);
    // Coalesce edits that arrive during a transition, including a return to
    // the original kernel. The last queued value must eventually be heard.
    if (frame == 6103) live.setReverbTimeRatio(1.0f);
    if (frame == 6104) live.setReverbTimeRatio(0.5f);
    const float input = frame == 0 ? 1.0f : 0.0f;
    const auto a = live.process({input, input});
    const auto b = reference.process({input, input});
    if (frame > 30000)
      require(std::fabs(a.left - b.left) < 0.000002f,
              "live decay edits must retain audio already in convolution history");
  }
  // Restoring unity and invalid inputs must restore the original response;
  // clamp out-of-range values without NaNs or amplification.
  for (float ratio : {1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()}) {
    live.setReverbTimeRatio(ratio);
    live.reset();
    const auto rendered = renderImpulse(live, 8192);
    require(std::fabs(rendered[4096].left - ir[4096 - 128]) < 0.000002f,
            "unity and invalid ratios must reproduce the original IR");
  }
  live.setReverbTimeRatio(-1.0f);
  reference.setReverbTimeRatio(0.25f);
  live.reset(); reference.reset();
  const auto a = renderImpulse(live, 8192), b = renderImpulse(reference, 8192);
  require(std::fabs(a[4096].left - b[4096].left) < 0.000002f,
          "ratios below 25 percent must clamp to 25 percent");
}

void verifyLiveDecayDoesNotClick()
{
  ardor::IrReverbProcessor reverb;
  std::string error;
  require(reverb.load(exponentialIr(1.2f), {}, kRate, error), error);
  reverb.setMix(1.0f); reverb.reset();
  float previous = 0.0f, maxStep = 0.0f;
  for (int frame = 0; frame < 80000; ++frame) {
    if (frame == 30001) reverb.setReverbTimeRatio(0.25f);
    if (frame == 50013) reverb.setReverbTimeRatio(1.0f);
    auditRealtimeMemory = true;
    const float output = reverb.process({0.01f, 0.01f}).left;
    auditRealtimeMemory = false;
    if (frame > 30000) maxStep = std::max(maxStep, std::fabs(output - previous));
    previous = output;
  }
  require(maxStep < 0.0003f, "live decay changes must crossfade without a discontinuity");
  require(realtimeAllocations == 0 && realtimeDeallocations == 0,
          "kernel handoff, rendering, promotion and retirement must not allocate or free on audio");
}

void verifyConcurrentDecayUpdates()
{
  ardor::IrReverbProcessor reverb;
  std::string error;
  require(reverb.load(exponentialIr(1.2f), {}, kRate, error), error);
  reverb.setMix(1.0f); reverb.reset();
  std::atomic<bool> started{false}, done{false};
  std::thread control([&] {
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int i = 0; i < 30; ++i) reverb.setReverbTimeRatio(i % 2 ? 1.0f : 0.25f);
    reverb.setReverbTimeRatio(0.5f);
    done.store(true, std::memory_order_release);
  });
  started.store(true, std::memory_order_release);
  while (!done.load(std::memory_order_acquire)) {
    const auto sample = reverb.process({0.001f, -0.001f});
    require(std::isfinite(sample.left) && std::isfinite(sample.right),
            "concurrent kernel preparation must leave realtime output finite");
  }
  control.join();
  reverb.reset();
  const auto rendered = renderImpulse(reverb, 8192);
  const float expected = 0.01f * std::exp(-6.90775527898f * (4096 - 128) / (kRate * 0.6f));
  require(std::fabs(rendered[4096].left - expected) < 0.000002f,
          "concurrent edits must apply the latest requested decay");
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
  verifyLiveControlsDoNotClick();
  verifyStereoImpulsesStayIndependent();
  verifyImpulseLengthIsCapped();
  verifyRejectsBadInput();
  verifyReverbTimeRatio();
  verifyUnmeasurableDecayIsUnchanged();
  verifyLiveDecayPreservesHistory();
  verifyLiveDecayDoesNotClick();
  verifyConcurrentDecayUpdates();
  std::printf("ir reverb smoke passed\n");
  return 0;
}
