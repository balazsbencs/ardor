#include "dsp/ParallelLaneExecutor.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct GainLane {
  float gain = 1.0f;
  std::atomic<unsigned> calls{0};
};

void processGain(void* opaque, const float* input, float* output, std::size_t frames)
{
  auto& lane = *static_cast<GainLane*>(opaque);
  lane.calls.fetch_add(1, std::memory_order_relaxed);
  for (std::size_t i = 0; i < frames; ++i) output[i] = input[i] * lane.gain;
}

bool near(float actual, float expected)
{
  return std::abs(actual - expected) < 1.0e-6f;
}

bool require(bool condition, const char* message)
{
  if (!condition) std::cerr << "parallel lane executor smoke: " << message << "\n";
  return condition;
}

ardor::ParallelLaneExecutorOptions testOptions(
  ardor::ParallelLaneExecutionMode mode)
{
  ardor::ParallelLaneExecutorOptions options;
  options.blockSize = 8;
  options.sampleRate = 48000.0;
  options.mode = mode;
  options.pipelineSlots = 2;
  options.requireWorkerSetup = true;
  options.requireRealtimeScheduling = false;
  options.requireAffinity = false;
  return options;
}

} // namespace

int main()
{
  constexpr std::size_t kFrames = 8;
  std::vector<float> input(kFrames);
  for (std::size_t i = 0; i < kFrames; ++i) input[i] = static_cast<float>(i + 1);

  GainLane direct0{1.0f};
  GainLane direct1{2.0f};
  GainLane direct2{-0.5f};
  std::vector<ardor::ParallelLane> directLanes{
    {&direct0, processGain, -1},
    {&direct1, processGain, -1},
    {&direct2, processGain, -1},
  };
  ardor::ParallelLaneExecutor direct;
  std::string error;
  auto directOptions = testOptions(ardor::ParallelLaneExecutionMode::Direct);
  directOptions.collectTiming = true;
  if (!require(direct.configure(std::move(directLanes),
                                directOptions,
                                error), error.c_str())) return 1;
  if (!require(direct.parallelEnabled(), "direct workers were not enabled")) return 1;

  ardor::ParallelLaneProcessResult directResult;
  if (!require(direct.processBlock(input.data(), kFrames, directResult),
               "direct process rejected a valid block")) return 1;
  if (!require(directResult.accepted && directResult.outputReady,
               "direct output was not ready")) return 1;
  if (!require(directResult.submittedGeneration == 1
                 && directResult.outputGeneration == 1,
               "direct generation handoff was incorrect")) return 1;
  for (std::size_t i = 0; i < kFrames; ++i) {
    if (!require(near(direct.laneOutput(0)[i], input[i]), "lane zero output mismatch")) return 1;
    if (!require(near(direct.laneOutput(1)[i], input[i] * 2.0f),
                 "lane one output mismatch")) return 1;
    if (!require(near(direct.laneOutput(2)[i], input[i] * -0.5f),
                 "lane two output mismatch")) return 1;
  }
  if (!require(direct0.calls.load() == 1 && direct1.calls.load() == 1
                 && direct2.calls.load() == 1,
               "direct callbacks did not run exactly once")) return 1;
  if (!require(direct.timing(0).calls == 1 && direct.timing(1).calls == 1
                 && direct.timing(2).calls == 1
                 && direct.timing(0).maximumNanoseconds > 0
                 && direct.directWaitNanoseconds() > 0,
               "direct timing telemetry was not recorded")) return 1;

  GainLane pipeline0{1.0f};
  GainLane pipeline1{3.0f};
  std::vector<ardor::ParallelLane> pipelineLanes{
    {&pipeline0, processGain, -1},
    {&pipeline1, processGain, -1},
  };
  ardor::ParallelLaneExecutor pipeline;
  auto pipelineOptions = testOptions(ardor::ParallelLaneExecutionMode::Pipelined);
  pipelineOptions.collectTiming = true;
  if (!require(pipeline.configure(std::move(pipelineLanes), pipelineOptions,
                                  error), error.c_str())) return 1;

  std::vector<float> secondInput(kFrames, 10.0f);
  ardor::ParallelLaneProcessResult firstPipeline;
  if (!require(pipeline.processBlock(input.data(), kFrames, firstPipeline),
               "pipeline rejected its first block")) return 1;
  if (!require(firstPipeline.accepted && !firstPipeline.outputReady,
               "pipeline exposed output before one block of latency")) return 1;

  ardor::ParallelLaneProcessResult nextPipeline;
  for (int attempt = 0; attempt < 250; ++attempt) {
    // Worker wake-up is scheduler-dependent even with the tiny test
    // callback.  Give the semaphore thread a bounded real wait rather than
    // relying on repeated yields, which can starve it on a busy host.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!require(pipeline.processBlock(secondInput.data(), kFrames, nextPipeline),
                 "pipeline rejected a valid block")) return 1;
    if (nextPipeline.outputReady) break;
  }
  if (!require(nextPipeline.outputReady, "pipeline never produced a completed block")) return 1;
  if (!require(nextPipeline.outputGeneration > 0
                 && nextPipeline.outputGeneration < nextPipeline.submittedGeneration,
               "pipeline output was not older than the current submission")) return 1;
  const float expected = nextPipeline.outputGeneration == 1 ? input[0] : secondInput[0];
  const float expectedLane1 = expected * 3.0f;
  if (!require(near(pipeline.laneOutput(0)[0], expected), "pipeline lane zero mismatch")) return 1;
  if (!require(near(pipeline.laneOutput(1)[0], expectedLane1), "pipeline worker lane mismatch")) return 1;
  if (!require(pipeline.timing(0).calls >= 2 && pipeline.timing(1).calls >= 1,
               "pipeline timing telemetry was not recorded")) return 1;
  pipeline.acknowledgeOutput();

  // A caller can deliberately request an impossible worker setup and still
  // get an explicit, deterministic sequential mode for tools/tests.
  GainLane fallback0{1.0f};
  GainLane fallback1{4.0f};
  std::vector<ardor::ParallelLane> fallbackLanes{
    {&fallback0, processGain, 9999},
    {&fallback1, processGain, 9999},
  };
  auto fallbackOptions = testOptions(ardor::ParallelLaneExecutionMode::Direct);
  fallbackOptions.requireWorkerSetup = false;
  fallbackOptions.requireAffinity = true;
  ardor::ParallelLaneExecutor fallback;
  if (!require(fallback.configure(std::move(fallbackLanes), fallbackOptions, error),
               error.c_str())) return 1;
  ardor::ParallelLaneProcessResult fallbackResult;
  if (!require(fallback.processBlock(input.data(), kFrames, fallbackResult),
               "sequential fallback rejected a valid block")) return 1;
  if (!require(fallbackResult.usedSequentialFallback && fallbackResult.outputReady,
               "sequential fallback was not reported")) return 1;
  if (!require(near(fallback.laneOutput(1)[3], input[3] * 4.0f),
               "sequential fallback output mismatch")) return 1;
  fallback.reset();
  if (!require(fallback.processBlock(input.data(), kFrames, fallbackResult),
               "sequential fallback failed after reset")) return 1;
  if (!require(fallbackResult.submittedGeneration == 1,
               "reset did not clear the fallback generation")) return 1;

  return 0;
}
