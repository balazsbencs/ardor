#include "dsp/FlexibleRoutingProgram.h"

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
  if (!condition) std::cerr << "flexible routing program smoke: " << message << "\n";
  return condition;
}

std::unique_ptr<ardor::RuntimeChain> emptyChain()
{
  return std::make_unique<ardor::RuntimeChain>();
}

std::unique_ptr<ardor::RuntimeChain> shortCabChain()
{
  auto chain = std::make_unique<ardor::RuntimeChain>();
  chain->addCab({1.0f, 0.5f}, 1.0f, 1.0f, "shared-cab");
  return chain;
}

ardor::FlexibleRoutingProgramOptions directOptions()
{
  ardor::FlexibleRoutingProgramOptions options;
  options.executor.blockSize = 8;
  options.executor.sampleRate = 48000.0;
  options.executor.mode = ardor::ParallelLaneExecutionMode::Direct;
  options.executor.requireWorkerSetup = false;
  options.executor.requireRealtimeScheduling = false;
  options.executor.requireAffinity = false;
  options.dry = {0.0f, 0.0f, false};
  options.admission.requireParallelForMultipleLanes = false;
  return options;
}

} // namespace

int main()
{
  constexpr std::size_t kFrames = 8;
  std::vector<float> input(kFrames, 1.0f);
  std::vector<float> left(kFrames, 0.0f);
  std::vector<float> right(kFrames, 0.0f);
  std::string error;

  ardor::FlexibleRoutingProgram empty;
  if (!require(!empty.prepare({}, directOptions(), error),
               "empty program topology was accepted")) return 1;

  ardor::FlexibleRoutingProgram missingChain;
  std::vector<ardor::FlexibleRoutingProgramLane> missing;
  missing.push_back({"missing", nullptr, {}, -1,
                     ardor::FlexibleRoutingLaneOutput::Downmix, false});
  if (!require(!missingChain.prepare(std::move(missing), directOptions(), error),
               "lane without a RuntimeChain was accepted")) return 1;

  ardor::FlexibleRoutingProgram unsafe;
  auto unsafeOptions = directOptions();
  unsafeOptions.executor.mode = ardor::ParallelLaneExecutionMode::Pipelined;
  std::vector<ardor::FlexibleRoutingProgramLane> unsafeLanes;
  unsafeLanes.push_back({"cab-lane", emptyChain(), {}, -1,
                         ardor::FlexibleRoutingLaneOutput::Downmix, true});
  if (!require(!unsafe.prepare(std::move(unsafeLanes), unsafeOptions, error),
               "pipelined per-lane convolution was not rejected")) return 1;

  ardor::FlexibleRoutingProgram unsafePostJoin;
  std::vector<ardor::FlexibleRoutingProgramLane> unsafePostLanes;
  unsafePostLanes.push_back({"lane", emptyChain(), {}, -1,
                             ardor::FlexibleRoutingLaneOutput::Downmix, false});
  std::vector<ardor::FlexibleRoutingProgramPostJoin> unsafePostJoins;
  unsafePostJoins.push_back({"shared-cab", emptyChain()});
  if (!require(!unsafePostJoin.prepare(std::move(unsafePostLanes),
                                       std::move(unsafePostJoins), unsafeOptions, error),
               "pipelined serial post-join stage was not rejected")) return 1;

  // The explicit dedicated post-stage worker is the admitted pipelined form.
  auto admittedOptions = directOptions();
  admittedOptions.executor.mode = ardor::ParallelLaneExecutionMode::Pipelined;
  admittedOptions.postJoinExecutor.mode =
    ardor::ParallelStereoStageExecutionMode::Pipelined;
  admittedOptions.postJoinExecutor.pipelineSlots = 2;
  admittedOptions.postJoinExecutor.requireWorkerSetup = false;
  admittedOptions.postJoinExecutor.requireRealtimeScheduling = false;
  admittedOptions.postJoinExecutor.requireAffinity = false;
  admittedOptions.admission.requireParallelForMultipleLanes = false;
  ardor::FlexibleRoutingProgram admitted;
  std::vector<ardor::FlexibleRoutingProgramLane> admittedLanes;
  admittedLanes.push_back({"lane-a", emptyChain(), {}, -1,
                           ardor::FlexibleRoutingLaneOutput::Downmix, false});
  admittedLanes.push_back({"lane-b", emptyChain(), {}, -1,
                           ardor::FlexibleRoutingLaneOutput::Downmix, false});
  std::vector<ardor::FlexibleRoutingProgramPostJoin> admittedJoins;
  admittedJoins.push_back({"shared-cab", shortCabChain()});
  if (!require(admitted.prepare(std::move(admittedLanes), std::move(admittedJoins),
                                admittedOptions, error), error.c_str())) return 1;
  if (!require(admitted.postJoinParallelEnabled(),
               "admitted program did not enable its post-stage worker")) return 1;
  ardor::FlexibleRoutingGraphProcessResult admittedResult;
  if (!require(admitted.processBlock(input.data(), left.data(), right.data(),
                                     kFrames, admittedResult),
               "admitted pipelined program rejected a valid block")) return 1;
  admitted.reset();

  ardor::FlexibleRoutingProgram program;
  auto options = directOptions();
  std::vector<ardor::FlexibleRoutingProgramLane> lanes;
  lanes.push_back({"left", emptyChain(), {1.0f, -1.0f, true}, -1,
                   ardor::FlexibleRoutingLaneOutput::Downmix, false});
  lanes.push_back({"right", emptyChain(), {1.0f, 1.0f, true}, -1,
                   ardor::FlexibleRoutingLaneOutput::Downmix, false});
  std::vector<ardor::FlexibleRoutingProgramPostJoin> postJoins;
  postJoins.push_back({"shared-cab", shortCabChain()});
  if (!require(program.prepare(std::move(lanes), std::move(postJoins), options, error),
               error.c_str())) return 1;
  if (!require(program.prepared() && program.laneCount() == 2
                 && program.blockSize() == kFrames
                 && program.laneId(0) == "left" && program.laneId(1) == "right"
                 && program.postJoinCount() == 1
                 && program.postJoinId(0) == "shared-cab",
               "prepared program topology was not retained")) return 1;

  ardor::FlexibleRoutingGraphProcessResult result;
  if (!require(program.processBlock(input.data(), left.data(), right.data(), kFrames, result),
               "prepared program rejected a valid block")) return 1;
  if (!require(result.accepted && result.wetReady && !result.usedFallback,
               "prepared program result flags were incorrect")) return 1;
  if (!require(near(left[0], 1.0f) && near(right[0], 1.0f),
               "owned lanes were not mixed with their configured pans")) return 1;
  if (!require(program.tailFrames() == 1 && program.underflowBlockCount() == 0,
               "prepared program exposed incorrect initial diagnostics")) return 1;

  program.reset();
  if (!require(program.processBlock(input.data(), left.data(), right.data(), kFrames, result),
               "prepared program could not process after reset")) return 1;
  if (!require(near(left[0], 1.0f) && near(right[0], 1.0f),
               "reset did not preserve prepared lane behavior")) return 1;
  return 0;
}
