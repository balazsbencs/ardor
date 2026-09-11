#include "dsp/WdwPairExecutor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

struct Lane {
  float gain = 1.0f;
  float rightScale = 1.0f;
  std::atomic<std::size_t> calls{0};
};

void processLane(void* opaque, const float* input, float* left, float* right,
                 std::size_t frames)
{
  auto* lane = static_cast<Lane*>(opaque);
  for (std::size_t i = 0; i < frames; ++i) {
    left[i] = input[i] * lane->gain;
    right[i] = left[i] * lane->rightScale;
  }
  lane->calls.fetch_add(1, std::memory_order_relaxed);
}

void requireAll(const std::vector<float>& values, float expected, const char* message)
{
  for (const float value : values) {
    if (std::fabs(value - expected) > 1.0e-6f) throw std::runtime_error(message);
  }
}

void directMode()
{
  Lane dry{2.0f, 1.0f};
  Lane wet{3.0f, -1.0f};
  ardor::WdwPairExecutor executor;
  std::string error;
  require(executor.configure(
            {&dry, &processLane, -1}, {&wet, &processLane, -1},
            {.blockSize = 8, .sampleRate = 48000.0,
             .mode = ardor::WdwPairExecutionMode::Direct,
             .collectTiming = true}, error), error.c_str());

  const std::vector<float> input(8, 0.25f);
  std::vector<float> dryLeft(8), dryRight(8), wetLeft(8), wetRight(8);
  ardor::WdwPairProcessResult result;
  require(executor.processBlock(input.data(), dryLeft.data(), dryRight.data(),
                                wetLeft.data(), wetRight.data(), input.size(), result),
          "direct process succeeds");
  require(result.accepted && result.outputReady && result.pairReady,
          "direct pair is ready");
  requireAll(dryLeft, 0.5f, "direct dry left");
  requireAll(dryRight, 0.5f, "direct dry right");
  requireAll(wetLeft, 0.75f, "direct wet left");
  requireAll(wetRight, -0.75f, "direct wet right");
  require(executor.timing(0).calls == 1 && executor.timing(1).calls == 1,
          "direct timing calls");
}

void pipelinedMode()
{
  Lane dry{2.0f, 1.0f};
  Lane wet{3.0f, -1.0f};
  ardor::WdwPairExecutor executor;
  std::string error;
  ardor::WdwPairExecutorOptions options;
  options.blockSize = 8;
  options.sampleRate = 48000.0;
  options.mode = ardor::WdwPairExecutionMode::Pipelined;
  options.pipelineSlots = 2;
  options.requireWorkerSetup = false;
  options.requireRealtimeScheduling = false;
  options.requireAffinity = false;
  options.collectTiming = true;
  options.maxHoldBlocks = 1;
  require(executor.configure({&dry, &processLane, -1}, {&wet, &processLane, -1},
                             options, error), error.c_str());
  require(executor.parallelEnabled() && executor.workersReady(),
          "pipelined workers ready");

  std::vector<float> input(8, 1.0f);
  std::vector<float> dryLeft(8), dryRight(8), wetLeft(8), wetRight(8);
  bool sawInitialSilence = false;
  bool sawPair = false;
  for (std::size_t block = 0; block < 200; ++block) {
    std::fill(input.begin(), input.end(), 1.0f + static_cast<float>(block));
    ardor::WdwPairProcessResult result;
    require(executor.processBlock(input.data(), dryLeft.data(), dryRight.data(),
                                  wetLeft.data(), wetRight.data(), input.size(), result),
            "pipelined process succeeds");
    if (!result.outputReady) {
      require(!result.pairReady, "unready output cannot be a pair");
      for (const float value : dryLeft) {
        if (value != 0.0f) throw std::runtime_error("initial output must be silent");
      }
      requireAll(dryRight, 0.0f, "initial dry-right output must be silent");
      requireAll(wetLeft, 0.0f, "initial wet-left output must be silent");
      requireAll(wetRight, 0.0f, "initial wet-right output must be silent");
      sawInitialSilence = true;
    }
    if (result.outputReady) {
      sawPair = true;
      // Both lane outputs come from one slot.  Their known gains make a
      // generation skew observable as a ratio mismatch.
      for (std::size_t i = 0; i < input.size(); ++i) {
        require(std::fabs(dryLeft[i] * 3.0f - wetLeft[i] * 2.0f) < 1.0e-5f,
                "dry/wet outputs share a generation");
        require(std::fabs(wetRight[i] + wetLeft[i]) < 1.0e-5f,
                "wet stereo output preserved");
      }
      require(result.outputAgeBlocks <= options.maxHoldBlocks,
              "pair age respects the bounded hold policy");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(sawInitialSilence, "pipelined startup is silent");
  require(sawPair, "pipelined pair eventually becomes ready");
  require(dry.calls.load(std::memory_order_relaxed) > 0 &&
            wet.calls.load(std::memory_order_relaxed) > 0,
          "both workers process blocks");
  require(executor.timing(0).calls > 0 && executor.timing(1).calls > 0,
          "worker timing calls");

  executor.reset();
  ardor::WdwPairProcessResult afterReset;
  std::fill(input.begin(), input.end(), 0.5f);
  require(executor.processBlock(input.data(), dryLeft.data(), dryRight.data(),
                                wetLeft.data(), wetRight.data(), input.size(), afterReset),
          "post-reset process succeeds");
  require(!afterReset.outputReady, "post-reset output waits for a new pair");
  requireAll(dryLeft, 0.0f, "post-reset dry silence");
}

} // namespace

int main()
{
  try {
    directMode();
    pipelinedMode();
  } catch (const std::exception& error) {
    return (void)error.what(), 1;
  }
  return 0;
}
