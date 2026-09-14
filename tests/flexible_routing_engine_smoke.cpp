#include "dsp/FlexibleRoutingProgram.h"
#include "dsp/PedalEngine.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool near(float actual, float expected)
{
  return std::fabs(actual - expected) < 1.0e-5f;
}

bool require(bool condition, const char* message)
{
  if (!condition) std::cerr << "flexible routing engine smoke: " << message << "\n";
  return condition;
}

std::unique_ptr<ardor::RuntimeChain> emptyChain()
{
  return std::make_unique<ardor::RuntimeChain>();
}

std::unique_ptr<ardor::FlexibleRoutingProgram> makeProgram(std::size_t frames)
{
  auto program = std::make_unique<ardor::FlexibleRoutingProgram>();
  ardor::FlexibleRoutingProgramOptions options;
  options.executor.blockSize = frames;
  options.executor.sampleRate = 48000.0;
  options.executor.mode = ardor::ParallelLaneExecutionMode::Direct;
  options.executor.requireWorkerSetup = false;
  options.executor.requireRealtimeScheduling = false;
  options.executor.requireAffinity = false;
  options.dry = {0.0f, 0.0f, false};
  options.admission.requireParallelForMultipleLanes = false;

  std::vector<ardor::FlexibleRoutingProgramLane> lanes;
  lanes.push_back({"left", emptyChain(), {1.0f, -1.0f, true}, -1,
                   ardor::FlexibleRoutingLaneOutput::Downmix, false});
  lanes.push_back({"right", emptyChain(), {1.0f, 1.0f, true}, -1,
                   ardor::FlexibleRoutingLaneOutput::Downmix, false});
  std::string error;
  if (!program->prepare(std::move(lanes), options, error)) {
    std::cerr << "flexible routing engine smoke: program prepare failed: " << error << "\n";
    return nullptr;
  }
  return program;
}

std::unique_ptr<ardor::RuntimeChain> shortCabChain(float scale)
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  chain->addCab({scale, 0.5f * scale}, 1.0f, 1.0f, "shared-cab");
  return chain;
}

std::unique_ptr<ardor::FlexibleRoutingProgram> makePipelinedProgram(
  std::size_t frames, float cabScale)
{
  auto program = std::make_unique<ardor::FlexibleRoutingProgram>();
  ardor::FlexibleRoutingProgramOptions options;
  options.executor.blockSize = frames;
  options.executor.sampleRate = 48000.0;
  options.executor.mode = ardor::ParallelLaneExecutionMode::Pipelined;
  options.executor.pipelineSlots = 2;
  options.executor.requireWorkerSetup = false;
  options.executor.requireRealtimeScheduling = false;
  options.executor.requireAffinity = false;
  options.postJoinExecutor.blockSize = frames;
  options.postJoinExecutor.sampleRate = 48000.0;
  options.postJoinExecutor.mode =
    ardor::ParallelStereoStageExecutionMode::Pipelined;
  options.postJoinExecutor.pipelineSlots = 2;
  options.postJoinExecutor.requireWorkerSetup = false;
  options.postJoinExecutor.requireRealtimeScheduling = false;
  options.postJoinExecutor.requireAffinity = false;
  options.dry = {0.0f, 0.0f, false};
  options.admission.requireParallelForMultipleLanes = false;

  std::vector<ardor::FlexibleRoutingProgramLane> lanes;
  lanes.push_back({"left", emptyChain(), {1.0f, -1.0f, true}, -1,
                   ardor::FlexibleRoutingLaneOutput::Downmix, false});
  lanes.push_back({"right", emptyChain(), {1.0f, 1.0f, true}, -1,
                   ardor::FlexibleRoutingLaneOutput::Downmix, false});
  std::vector<ardor::FlexibleRoutingProgramPostJoin> postJoins;
  postJoins.push_back({"shared-cab", shortCabChain(cabScale)});

  std::string error;
  if (!program->prepare(std::move(lanes), std::move(postJoins), options, error)) {
    std::cerr << "flexible routing engine smoke: pipelined program prepare failed: "
              << error << "\n";
    return nullptr;
  }
  return program;
}

} // namespace

int main()
{
  constexpr std::size_t kFrames = 8;
  std::string error;

  ardor::PedalEngine unprepared;
  if (!require(!unprepared.installPreparedRouting(
                 std::make_unique<ardor::FlexibleRoutingProgram>(), error),
               "unprepared routing was installed")) return 1;

  ardor::PedalEngine engine;
  engine.prepareBlockSize(kFrames);
  engine.setSafetyLimiterEnabled(false);
  auto program = makeProgram(kFrames);
  if (!require(program != nullptr, "routing program could not be built")) return 1;
  if (!require(engine.installPreparedRouting(std::move(program), error), error.c_str())) return 1;
  if (!require(engine.flexibleRoutingEnabled(), "engine did not publish the routing owner")) return 1;

  std::vector<float> input(kFrames, 1.0f);
  std::vector<float> left(kFrames, 0.0f);
  std::vector<float> right(kFrames, 0.0f);
  engine.processBlock(input.data(), left.data(), right.data(), kFrames);
  if (!require(near(left[0], 1.0f) && near(right[0], 1.0f),
               "engine block path did not use the prepared lanes")) return 1;

  engine.reset();
  const auto sample = engine.process(1.0f);
  if (!require(near(sample.first, 1.0f) && near(sample.second, 1.0f),
               "engine sample compatibility path diverged from block routing")) return 1;
  if (!require(engine.tailFrames() == 0, "engine reported an unexpected routing tail")) return 1;

  engine.clearPreparedRouting();
  if (!require(!engine.flexibleRoutingEnabled(), "routing owner was not cleared")) return 1;

  ardor::PedalEngine mismatched;
  mismatched.prepareBlockSize(4);
  auto wrongSize = makeProgram(kFrames);
  if (!require(wrongSize != nullptr, "mismatch program could not be built")) return 1;
  if (!require(!mismatched.installPreparedRouting(std::move(wrongSize), error),
               "mismatched routing block size was accepted")) return 1;

  // Exercise the control-thread lifecycle boundary while both the lane and
  // dedicated post-join workers are receiving blocks. Each replacement must
  // stop the old worker graph before its callback contexts are destroyed;
  // reset must also discard generations without leaving stale output pointers.
  ardor::PedalEngine lifecycle;
  lifecycle.prepareBlockSize(kFrames);
  lifecycle.setSafetyLimiterEnabled(false);
  std::vector<float> lifecycleInput(kFrames, 0.25f);
  std::vector<float> lifecycleLeft(kFrames, 0.0f);
  std::vector<float> lifecycleRight(kFrames, 0.0f);
  for (int iteration = 0; iteration < 8; ++iteration) {
    auto pipelined = makePipelinedProgram(
      kFrames, 1.0f + static_cast<float>(iteration) * 0.05f);
    if (!require(pipelined != nullptr, "pipelined lifecycle program could not be built")) {
      return 1;
    }
    if (!require(lifecycle.installPreparedRouting(std::move(pipelined), error),
                 error.c_str())) return 1;

    for (int block = 0; block < 8; ++block) {
      lifecycle.processBlock(lifecycleInput.data(), lifecycleLeft.data(),
                             lifecycleRight.data(), kFrames);
      for (std::size_t frame = 0; frame < kFrames; ++frame) {
        if (!require(std::isfinite(lifecycleLeft[frame])
                       && std::isfinite(lifecycleRight[frame]),
                     "pipelined lifecycle output became non-finite")) return 1;
      }
    }
    lifecycle.reset();
    lifecycle.processBlock(lifecycleInput.data(), lifecycleLeft.data(),
                           lifecycleRight.data(), kFrames);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
      if (!require(std::isfinite(lifecycleLeft[frame])
                     && std::isfinite(lifecycleRight[frame]),
                   "reset left stale/non-finite pipelined output")) return 1;
    }
  }
  lifecycle.clearPreparedRouting();
  if (!require(!lifecycle.flexibleRoutingEnabled(),
               "lifecycle routing owner was not cleared")) return 1;

  return 0;
}
