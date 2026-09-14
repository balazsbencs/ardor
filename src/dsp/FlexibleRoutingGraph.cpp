#include "dsp/FlexibleRoutingGraph.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace ardor {

bool FlexibleRoutingGraph::configure(
  std::vector<FlexibleRoutingLane> lanes,
  std::vector<FlexibleRoutingJoinStage> joinStages,
  FlexibleRoutingGraphOptions options,
  std::string& error)
{
  error.clear();
  // configure() is a control-thread operation. Tear down a previous worker
  // topology before validating a replacement so an invalid snapshot cannot
  // leave callbacks running against partially cleared graph state.
  executor_.clear();
  postJoinExecutor_.clear();
  configured_ = false;
  sequentialFallback_ = false;
  blockSize_ = 0;
  laneIds_.clear();
  laneResets_.clear();
  joinStages_.clear();
  laneInputs_.clear();
  mixedLeft_.clear();
  mixedRight_.clear();

  if (lanes.empty()) {
    error = "flexible routing graph requires at least one lane";
    return false;
  }
  std::unordered_set<std::string> ids;
  for (const auto& lane : lanes) {
    if (lane.id.empty()) {
      error = "flexible routing graph requires stable lane IDs";
      return false;
    }
    if (!ids.insert(lane.id).second) {
      error = "flexible routing graph contains a duplicate stage ID: " + lane.id;
      return false;
    }
    if (!lane.process) {
      error = "flexible routing graph received a lane without a process callback";
      return false;
    }
  }
  for (const auto& stage : joinStages) {
    if (stage.id.empty()) {
      error = "flexible routing graph requires stable join stage IDs";
      return false;
    }
    if (!ids.insert(stage.id).second) {
      error = "flexible routing graph contains a duplicate stage ID: " + stage.id;
      return false;
    }
    if (!stage.process) {
      error = "flexible routing graph received a join stage without a process callback";
      return false;
    }
  }
  if (options.executor.blockSize == 0
      || !std::isfinite(options.executor.sampleRate)
      || options.executor.sampleRate <= 0.0) {
    error = "flexible routing graph requires a valid block size and sample rate";
    return false;
  }
  if (!joinStages.empty()
      && options.postJoinExecutor.blockSize != 0
      && options.postJoinExecutor.blockSize != options.executor.blockSize) {
    error = "flexible routing graph post-join block size must match its lane quantum";
    return false;
  }

  laneIds_.reserve(lanes.size());
  laneResets_.reserve(lanes.size());
  std::vector<ParallelLane> executorLanes;
  executorLanes.reserve(lanes.size());
  for (auto& lane : lanes) {
    laneIds_.push_back(std::move(lane.id));
    laneResets_.push_back({lane.context, lane.reset});
    executorLanes.push_back({lane.context, lane.process, lane.workerCpu});
  }

  if (!mixer_.prepare(lanes.size(), options.executor.blockSize)) {
    error = "flexible routing graph could not prepare its lane mixer";
    laneIds_.clear();
    laneResets_.clear();
    return false;
  }
  for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
    if (!mixer_.setLane(lane, lanes[lane].mix)) {
      error = "flexible routing graph could not configure a lane mix";
      laneIds_.clear();
      laneResets_.clear();
      return false;
    }
  }
  mixer_.setDry(options.dry);
  mixer_.setUnderflowPolicy(options.underflowPolicy);

  laneInputs_.assign(lanes.size(), nullptr);
  mixedLeft_.assign(options.executor.blockSize, 0.0f);
  mixedRight_.assign(options.executor.blockSize, 0.0f);
  joinStages_ = std::move(joinStages);

  if (!joinStages_.empty()) {
    if (options.postJoinExecutor.blockSize == 0) {
      options.postJoinExecutor.blockSize = options.executor.blockSize;
    }
    if (options.postJoinExecutor.sampleRate <= 0.0) {
      options.postJoinExecutor.sampleRate = options.executor.sampleRate;
    }
    if (!postJoinExecutor_.configure(&FlexibleRoutingGraph::processJoinStages, this,
                                     options.postJoinExecutor, error)) {
      laneIds_.clear();
      laneResets_.clear();
      joinStages_.clear();
      laneInputs_.clear();
      mixedLeft_.clear();
      mixedRight_.clear();
      mixer_.reset();
      return false;
    }
  }

  if (!executor_.configure(std::move(executorLanes), options.executor, error)) {
    postJoinExecutor_.clear();
    laneIds_.clear();
    laneResets_.clear();
    joinStages_.clear();
    laneInputs_.clear();
    mixedLeft_.clear();
    mixedRight_.clear();
    mixer_.reset();
    return false;
  }

  blockSize_ = options.executor.blockSize;
  sequentialFallback_ = !executor_.parallelEnabled();
  configured_ = true;
  return true;
}

bool FlexibleRoutingGraph::processBlock(
  const float* input, float* outputLeft, float* outputRight,
  std::size_t frames, FlexibleRoutingGraphProcessResult& result) noexcept
{
  result = {};
  if (!configured_ || !input || !outputLeft || !outputRight
      || frames != blockSize_) {
    return false;
  }

  ParallelLaneProcessResult executorResult;
  if (!executor_.processBlock(input, frames, executorResult)) return false;

  for (std::size_t lane = 0; lane < laneInputs_.size(); ++lane) {
    laneInputs_[lane] = executorResult.outputReady
      ? executor_.laneOutput(lane) : nullptr;
  }
  if (!mixer_.processBlock(input, laneInputs_.data(), executorResult.outputReady,
                           mixedLeft_.data(), mixedRight_.data(), frames)) {
    return false;
  }
  if (executorResult.outputReady) executor_.acknowledgeOutput();

  if (joinStages_.empty()) {
    std::copy(mixedLeft_.begin(), mixedLeft_.begin() + static_cast<std::ptrdiff_t>(frames),
              outputLeft);
    std::copy(mixedRight_.begin(), mixedRight_.begin() + static_cast<std::ptrdiff_t>(frames),
              outputRight);
    result.postJoinAccepted = true;
    result.postJoinReady = true;
  } else {
    ParallelStereoStageProcessResult postResult;
    if (!postJoinExecutor_.processBlock(mixedLeft_.data(), mixedRight_.data(),
                                        outputLeft, outputRight, frames,
                                        postResult)) {
      return false;
    }
    result.postJoinAccepted = postResult.accepted;
    result.postJoinReady = postResult.outputReady;
    result.usedPostJoinFallback = postResult.usedFallback;
    result.postJoinSubmittedGeneration = postResult.submittedGeneration;
    result.postJoinOutputGeneration = postResult.outputGeneration;
  }

  result.accepted = executorResult.accepted;
  result.wetReady = executorResult.outputReady;
  result.usedFallback = !executorResult.outputReady;
  result.workersReady = workersReady();
  result.usedSequentialFallback = executorResult.usedSequentialFallback;
  result.submittedGeneration = executorResult.submittedGeneration;
  result.outputGeneration = executorResult.outputGeneration;
  return true;
}

void FlexibleRoutingGraph::reset() noexcept
{
  if (!configured_) return;
  executor_.reset();
  mixer_.reset();
  postJoinExecutor_.reset();
  for (const auto& target : laneResets_) {
    if (target.reset) target.reset(target.context);
  }
  for (auto& stage : joinStages_) {
    if (stage.reset) stage.reset(stage.context);
  }
  std::fill(mixedLeft_.begin(), mixedLeft_.end(), 0.0f);
  std::fill(mixedRight_.begin(), mixedRight_.end(), 0.0f);
  std::fill(laneInputs_.begin(), laneInputs_.end(), nullptr);
}

void FlexibleRoutingGraph::processJoinStages(void* opaque, float* left,
                                              float* right,
                                              std::size_t frames) noexcept
{
  auto* graph = static_cast<FlexibleRoutingGraph*>(opaque);
  if (!graph || !left || !right) return;
  for (auto& stage : graph->joinStages_) {
    if (stage.process) stage.process(stage.context, left, right, frames);
  }
}

} // namespace ardor
