#include "dsp/FlexibleRoutingGraph.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct LanePipeline {
  std::array<float, 3> gains{};
  std::size_t count = 0;
  std::size_t calls = 0;
  std::size_t resetCalls = 0;
};

void processLane(void* opaque, const float* input, float* output, std::size_t frames)
{
  auto& lane = *static_cast<LanePipeline*>(opaque);
  ++lane.calls;
  for (std::size_t i = 0; i < frames; ++i) {
    float value = input[i];
    for (std::size_t stage = 0; stage < lane.count; ++stage) value *= lane.gains[stage];
    output[i] = value;
  }
}

struct JoinGain {
  float left = 1.0f;
  float right = 1.0f;
  std::size_t calls = 0;
  std::size_t resetCalls = 0;
};

struct GatedJoin {
  std::atomic<bool> release{false};
  float left = 2.0f;
  float right = 3.0f;
};

void processGatedJoin(void* opaque, float* left, float* right, std::size_t frames)
{
  auto& stage = *static_cast<GatedJoin*>(opaque);
  while (!stage.release.load(std::memory_order_acquire)) std::this_thread::yield();
  for (std::size_t i = 0; i < frames; ++i) {
    left[i] *= stage.left;
    right[i] *= stage.right;
  }
}

void processJoin(void* opaque, float* left, float* right, std::size_t frames)
{
  auto& stage = *static_cast<JoinGain*>(opaque);
  ++stage.calls;
  for (std::size_t i = 0; i < frames; ++i) {
    left[i] *= stage.left;
    right[i] *= stage.right;
  }
}

void resetLane(void* opaque)
{
  ++static_cast<LanePipeline*>(opaque)->resetCalls;
}

void resetJoin(void* opaque)
{
  ++static_cast<JoinGain*>(opaque)->resetCalls;
}

bool near(float actual, float expected)
{
  return std::abs(actual - expected) < 1.0e-5f;
}

bool require(bool condition, const char* message)
{
  if (!condition) std::cerr << "flexible routing graph smoke: " << message << "\n";
  return condition;
}

ardor::FlexibleRoutingGraphOptions options(ardor::ParallelLaneExecutionMode mode)
{
  ardor::FlexibleRoutingGraphOptions result;
  result.executor.blockSize = 8;
  result.executor.sampleRate = 48000.0;
  result.executor.mode = mode;
  result.executor.pipelineSlots = 2;
  result.executor.requireWorkerSetup = true;
  result.executor.requireRealtimeScheduling = false;
  result.executor.requireAffinity = false;
  result.dry = {0.0f, 0.0f, false};
  return result;
}

} // namespace

int main()
{
  constexpr std::size_t kFrames = 8;
  constexpr float kInput = 2.0f;
  std::vector<float> input(kFrames, kInput);
  std::vector<float> left(kFrames, 0.0f);
  std::vector<float> right(kFrames, 0.0f);

  std::string error;
  ardor::FlexibleRoutingGraph invalid;
  if (!require(!invalid.configure({}, {}, options(ardor::ParallelLaneExecutionMode::Direct),
                                   error),
               "empty graph topology was accepted")) return 1;

  LanePipeline leftLane{{2.0f, 0.5f, 1.0f}, 3};
  LanePipeline rightLane{{3.0f, 1.0f, 1.0f}, 2};
  JoinGain joinA{0.5f, 2.0f};
  JoinGain joinB{2.0f, 0.25f};
  std::vector<ardor::FlexibleRoutingLane> lanes{
    {"left", &leftLane, processLane, -1, {1.0f, -1.0f, true}, resetLane},
    {"right", &rightLane, processLane, -1, {1.0f, 1.0f, true}, resetLane},
  };
  std::vector<ardor::FlexibleRoutingJoinStage> joins{
    {"post-gain", &joinA, processJoin, resetJoin},
    {"post-trim", &joinB, processJoin, resetJoin},
  };

  ardor::FlexibleRoutingGraph duplicate;
  std::vector<ardor::FlexibleRoutingLane> duplicateLanes{
    {"same", &leftLane, processLane, -1, {}},
    {"same", &rightLane, processLane, -1, {}},
  };
  if (!require(!duplicate.configure(std::move(duplicateLanes), {},
                                    options(ardor::ParallelLaneExecutionMode::Direct),
                                    error),
               "duplicate graph stage IDs were accepted")) return 1;

  ardor::FlexibleRoutingGraph graph;
  auto directOptions = options(ardor::ParallelLaneExecutionMode::Direct);
  directOptions.executor.collectTiming = true;
  if (!require(graph.configure(std::move(lanes), std::move(joins),
                               directOptions, error),
               error.c_str())) return 1;
  if (!require(graph.laneCount() == 2 && graph.joinStageCount() == 2,
               "graph topology dimensions were incorrect")) return 1;
  if (!require(graph.laneId(0) == "left" && graph.laneId(1) == "right"
                 && graph.joinStageId(0) == "post-gain"
                 && graph.joinStageId(1) == "post-trim"
                 && graph.laneId(2).empty(),
               "graph stage IDs were not retained immutably")) return 1;
  if (!require(graph.parallelEnabled(), "direct graph workers were not enabled")) return 1;

  ardor::FlexibleRoutingGraphProcessResult result;
  if (!require(graph.processBlock(input.data(), left.data(), right.data(), kFrames, result),
               "direct graph rejected a valid block")) return 1;
  if (!require(result.accepted && result.wetReady && !result.usedFallback,
               "direct graph result flags were incorrect")) return 1;
  // left: 2 * (2 * .5 * 1) * 2 = 2; right: 2 * (3 * 1 * 2) * .25 = 3.
  if (!require(near(left[0], 2.0f) && near(right[0], 3.0f),
               "sequential lane or join stages were ordered incorrectly")) return 1;
  if (!require(leftLane.calls == 1 && rightLane.calls == 1
                 && joinA.calls == 1 && joinB.calls == 1,
               "graph callbacks did not run exactly once")) return 1;
  if (!require(graph.laneTiming(0).calls == 1 && graph.laneTiming(1).calls == 1
                 && graph.laneTiming(0).maximumNanoseconds > 0
                 && graph.laneTiming(1).maximumNanoseconds > 0,
               "optional lane timing telemetry was not recorded")) return 1;

  // A pipelined graph emits the configured dry fallback until its first wet
  // block completes, then exposes a completed lane generation.
  LanePipeline pipelineLeft{{1.0f, 1.0f, 1.0f}, 1};
  LanePipeline pipelineRight{{1.0f, 1.0f, 1.0f}, 1};
  JoinGain pipelineJoin{1.0f, 1.0f};
  std::vector<ardor::FlexibleRoutingLane> pipelineLanes{
    {"left", &pipelineLeft, processLane, -1, {1.0f, -1.0f, true}, resetLane},
    {"right", &pipelineRight, processLane, -1, {1.0f, 1.0f, true}, resetLane},
  };
  std::vector<ardor::FlexibleRoutingJoinStage> pipelineJoins{
    {"post", &pipelineJoin, processJoin, resetJoin},
  };
  auto pipelineOptions = options(ardor::ParallelLaneExecutionMode::Pipelined);
  pipelineOptions.dry = {1.0f, 0.0f, true};
  pipelineOptions.underflowPolicy = ardor::ParallelLaneUnderflowPolicy::DryFallback;
  ardor::FlexibleRoutingGraph pipeline;
  if (!require(pipeline.configure(std::move(pipelineLanes), std::move(pipelineJoins),
                                  pipelineOptions, error), error.c_str())) return 1;
  ardor::FlexibleRoutingGraphProcessResult first;
  if (!require(pipeline.processBlock(input.data(), left.data(), right.data(), kFrames, first),
               "pipelined graph rejected its first block")) return 1;
  if (!require(first.accepted && !first.wetReady && first.usedFallback,
               "pipelined graph did not report its initial dry fallback")) return 1;
  const float expectedDry = kInput * 0.70710678118f;
  if (!require(near(left[0], expectedDry) && near(right[0], expectedDry),
               "pipelined dry fallback was mixed incorrectly")) return 1;

  ardor::FlexibleRoutingGraphProcessResult next;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (!require(pipeline.processBlock(input.data(), left.data(), right.data(), kFrames, next),
               "pipelined graph rejected a valid block")) return 1;
  if (!require(next.wetReady, "pipelined graph never exposed a completed wet block")) return 1;
  if (!require(pipeline.underflowBlockCount() >= 1,
               "pipelined graph did not count its initial underflow")) return 1;
  if (!require(pipeline.submissionMissCount() == 0,
               "pipelined graph unexpectedly missed a ring submission")) return 1;

  // A dedicated post-stage worker makes the serial join explicit: the first
  // block is bypassed while the worker is gated, then a completed generation
  // is exposed without ever waiting on the callback.
  LanePipeline postLane{{1.0f, 1.0f, 1.0f}, 1};
  GatedJoin gatedJoin;
  std::vector<ardor::FlexibleRoutingLane> postLanes{
    {"lane", &postLane, processLane, -1, {1.0f, 0.0f, true}, resetLane},
  };
  std::vector<ardor::FlexibleRoutingJoinStage> postJoins{
    {"gated-post", &gatedJoin, processGatedJoin, nullptr},
  };
  auto postOptions = options(ardor::ParallelLaneExecutionMode::Direct);
  postOptions.postJoinExecutor.mode =
    ardor::ParallelStereoStageExecutionMode::Pipelined;
  postOptions.postJoinExecutor.pipelineSlots = 2;
  postOptions.postJoinExecutor.requireWorkerSetup = true;
  postOptions.postJoinExecutor.requireRealtimeScheduling = false;
  postOptions.postJoinExecutor.requireAffinity = false;
  postOptions.postJoinExecutor.collectTiming = true;
  ardor::FlexibleRoutingGraph postPipeline;
  if (!require(postPipeline.configure(std::move(postLanes), std::move(postJoins),
                                      postOptions, error), error.c_str())) return 1;
  if (!require(postPipeline.postJoinParallelEnabled(),
               "dedicated post-stage worker was not enabled")) return 1;
  if (!require(postPipeline.processBlock(input.data(), left.data(), right.data(),
                                         kFrames, result),
               "post-stage pipeline rejected its first block")) return 1;
  if (!require(result.postJoinAccepted && !result.postJoinReady
                 && result.usedPostJoinFallback,
               "post-stage pipeline did not report its bounded initial bypass")) return 1;
  const float prePostLeft = left[0];
  const float prePostRight = right[0];
  gatedJoin.release.store(true, std::memory_order_release);
  bool postReady = false;
  for (int attempt = 0; attempt < 100 && !postReady; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!require(postPipeline.processBlock(input.data(), left.data(), right.data(),
                                           kFrames, result),
                 "post-stage pipeline rejected a follow-up block")) return 1;
    postReady = result.postJoinReady;
  }
  if (!require(postReady, "post-stage pipeline never exposed a completed generation")) return 1;
  if (!require(near(left[0], prePostLeft * gatedJoin.left)
                 && near(right[0], prePostRight * gatedJoin.right),
               "post-stage worker did not preserve ordered stereo processing")) return 1;
  if (!require(postPipeline.postJoinUnderflowBlockCount() >= 1,
               "post-stage pipeline did not count its initial fallback")) return 1;
  if (!require(postPipeline.postJoinTiming().calls > 0
                 && postPipeline.postJoinTiming().maximumNanoseconds > 0,
               "optional post-stage timing telemetry was not recorded")) return 1;

  ardor::FlexibleRoutingGraph mismatchedPost;
  LanePipeline mismatchLane{{1.0f, 1.0f, 1.0f}, 1};
  JoinGain mismatchJoin;
  auto mismatchOptions = options(ardor::ParallelLaneExecutionMode::Direct);
  mismatchOptions.postJoinExecutor.blockSize = 4;
  std::vector<ardor::FlexibleRoutingLane> mismatchLanes{
    {"lane", &mismatchLane, processLane, -1, {}, resetLane},
  };
  std::vector<ardor::FlexibleRoutingJoinStage> mismatchJoins{
    {"post", &mismatchJoin, processJoin, resetJoin},
  };
  if (!require(!mismatchedPost.configure(std::move(mismatchLanes),
                                         std::move(mismatchJoins),
                                         mismatchOptions, error),
               "post-stage block-size mismatch was accepted")) return 1;

  // Reconfiguration is a control-thread lifecycle operation. An invalid
  // replacement must stop the previous worker topology rather than leave it
  // attached to cleared callback descriptors.
  ardor::FlexibleRoutingGraph reconfigurable;
  LanePipeline reconfigLane{{1.0f, 1.0f, 1.0f}, 1};
  std::vector<ardor::FlexibleRoutingLane> reconfigLanes{
    {"reconfig", &reconfigLane, processLane, -1, {}, resetLane},
  };
  if (!require(reconfigurable.configure(std::move(reconfigLanes), {},
                                        options(ardor::ParallelLaneExecutionMode::Direct), error),
               error.c_str())) return 1;
  if (!require(!reconfigurable.configure({}, {},
                                          options(ardor::ParallelLaneExecutionMode::Direct), error),
               "invalid graph replacement was accepted")) return 1;
  if (!require(reconfigurable.blockSize() == 0 && !reconfigurable.processBlock(
                 input.data(), left.data(), right.data(), kFrames, result),
               "invalid graph replacement left stale prepared state")) return 1;

  graph.reset();
  pipeline.reset();
  if (!require(leftLane.resetCalls == 1 && rightLane.resetCalls == 1
                 && joinA.resetCalls == 1 && joinB.resetCalls == 1
                 && pipelineLeft.resetCalls == 1 && pipelineRight.resetCalls == 1
                 && pipelineJoin.resetCalls == 1,
               "graph reset did not reset every configured stage")) return 1;
  return 0;
}
