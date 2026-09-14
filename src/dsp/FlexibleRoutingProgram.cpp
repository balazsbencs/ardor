#include "dsp/FlexibleRoutingProgram.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ardor {

namespace {

constexpr float kQuarterPi = 0.78539816339744830962f;

void normalizeMix(ParallelLaneMixConfig config, float& left, float& right,
                  bool& enabled) noexcept
{
  const float level = std::isfinite(config.level) ? std::max(config.level, 0.0f) : 0.0f;
  const float pan = std::isfinite(config.pan) ? std::clamp(config.pan, -1.0f, 1.0f) : 0.0f;
  const float angle = (pan + 1.0f) * kQuarterPi;
  left = level * std::cos(angle);
  right = level * std::sin(angle);
  enabled = config.enabled;
}

} // namespace

bool FlexibleRoutingProgram::prepare(
  std::vector<FlexibleRoutingProgramLane> lanes,
  FlexibleRoutingProgramOptions options,
  std::string& error)
{
  return prepare(std::move(lanes), {}, std::move(options), error);
}

bool FlexibleRoutingProgram::prepare(
  std::vector<FlexibleRoutingProgramLane> lanes,
  std::vector<FlexibleRoutingProgramPostJoin> postJoins,
  FlexibleRoutingProgramOptions options,
  std::string& error)
{
  error.clear();
  if (prepared_) {
    error = "flexible routing program cannot be reconfigured after prepare";
    return false;
  }
  if (lanes.empty()) {
    error = "flexible routing program requires at least one lane";
    return false;
  }
  if (options.admission.maxLanes > 0
      && lanes.size() > options.admission.maxLanes) {
    error = "flexible routing program exceeds its lane admission limit";
    return false;
  }
  if (options.executor.blockSize == 0
      || !std::isfinite(options.executor.sampleRate)
      || options.executor.sampleRate <= 0.0) {
    error = "flexible routing program requires a valid block size and sample rate";
    return false;
  }
  if (options.executor.mode == ParallelLaneExecutionMode::Pipelined
      && options.admission.rejectPipelinedPerLaneConvolution) {
    for (const auto& lane : lanes) {
      if (lane.hasPerLaneConvolution) {
        error = "pipelined flexible routing rejects per-lane convolution: " + lane.id
          + "; use direct mode or a shared post-join cabinet";
        return false;
      }
    }
  }
  if (options.executor.mode == ParallelLaneExecutionMode::Pipelined
      && options.admission.rejectPipelinedPostJoin
      && !postJoins.empty()
      && options.postJoinExecutor.mode != ParallelStereoStageExecutionMode::Pipelined) {
    error = "pipelined flexible routing does not admit serial post-join stages yet;"
      " select the dedicated post-stage pipeline or use direct mode";
    return false;
  }

  std::vector<FlexibleRoutingLane> graphLanes;
  std::vector<FlexibleRoutingJoinStage> graphJoins;
  graphLanes.reserve(lanes.size());
  graphJoins.reserve(postJoins.size());
  lanes_.reserve(lanes.size());
  postJoins_.reserve(postJoins.size());
  for (auto& lane : lanes) {
    if (!lane.chain) {
      error = "flexible routing program received a lane without a RuntimeChain: "
        + lane.id;
      lanes_.clear();
      postJoins_.clear();
      return false;
    }

    auto context = std::make_unique<LaneContext>();
    context->chain = std::move(lane.chain);
    context->output = lane.output;
    normalizeMix(lane.mix, context->leftGain, context->rightGain, context->enabled);
    context->chain->prepareBlockSize(options.executor.blockSize);
    context->left.assign(options.executor.blockSize, 0.0f);
    context->right.assign(options.executor.blockSize, 0.0f);
    LaneContext* rawContext = context.get();
    lanes_.push_back(std::move(context));

    graphLanes.push_back({
      std::move(lane.id),
      rawContext,
      &FlexibleRoutingProgram::processLane,
      lane.workerCpu,
      lane.mix,
      &FlexibleRoutingProgram::resetLane,
    });
  }

  for (auto& postJoin : postJoins) {
    if (!postJoin.chain) {
      error = "flexible routing program received a post-join stage without a RuntimeChain: "
        + postJoin.id;
      lanes_.clear();
      postJoins_.clear();
      return false;
    }

    auto context = std::make_unique<PostJoinContext>();
    context->chain = std::move(postJoin.chain);
    context->chain->prepareBlockSize(options.executor.blockSize);
    context->mono.assign(options.executor.blockSize, 0.0f);
    PostJoinContext* rawContext = context.get();
    postJoins_.push_back(std::move(context));

    graphJoins.push_back({
      std::move(postJoin.id),
      rawContext,
      &FlexibleRoutingProgram::processPostJoin,
      &FlexibleRoutingProgram::resetPostJoin,
    });
  }

  auto graph = std::make_unique<FlexibleRoutingGraph>();
  if (!graph->configure(std::move(graphLanes), std::move(graphJoins),
                        FlexibleRoutingGraphOptions{
                          options.executor,
                          options.dry,
                          options.underflowPolicy,
                          options.postJoinExecutor,
                        }, error)) {
    graph.reset();
    lanes_.clear();
    postJoins_.clear();
    return false;
  }
  if (lanes.size() > 1 && options.admission.requireParallelForMultipleLanes
      && !graph->parallelEnabled()) {
    error = "flexible routing program could not admit multiple lanes without parallel workers";
    graph.reset();
    lanes_.clear();
    postJoins_.clear();
    return false;
  }

  graph_ = std::move(graph);
  bool dryEnabled = false;
  normalizeMix(options.dry, dryLeftGain_, dryRightGain_, dryEnabled);
  if (!dryEnabled) {
    dryLeftGain_ = 0.0f;
    dryRightGain_ = 0.0f;
  }
  prepared_ = true;
  return true;
}

bool FlexibleRoutingProgram::processBlock(
  const float* input, float* outputLeft, float* outputRight,
  std::size_t frames, FlexibleRoutingGraphProcessResult& result) noexcept
{
  if (!graph_) {
    result = {};
    return false;
  }
  return graph_->processBlock(input, outputLeft, outputRight, frames, result);
}

bool FlexibleRoutingProgram::processSample(float input, float& outputLeft,
                                            float& outputRight) noexcept
{
  // A pipelined post-stage owns block-sized state on its worker. The legacy
  // one-sample compatibility API cannot preserve that latency contract, so
  // reject it rather than concurrently touching the same RuntimeChain.
  if (!graph_ || graph_->postJoinParallelEnabled() || !std::isfinite(input)) {
    return false;
  }

  float wetLeft = 0.0f;
  float wetRight = 0.0f;
  for (const auto& lane : lanes_) {
    if (!lane || !lane->chain || !lane->enabled) continue;
    const StereoSample stereo = lane->chain->process({input, input});
    float mono = stereo.left;
    switch (lane->output) {
    case FlexibleRoutingLaneOutput::Left:
      mono = stereo.left;
      break;
    case FlexibleRoutingLaneOutput::Right:
      mono = stereo.right;
      break;
    case FlexibleRoutingLaneOutput::Downmix:
      mono = (stereo.left + stereo.right) * 0.5f;
      break;
    }
    wetLeft += mono * lane->leftGain;
    wetRight += mono * lane->rightGain;
  }
  outputLeft = input * dryLeftGain_ + wetLeft;
  outputRight = input * dryRightGain_ + wetRight;
  for (const auto& postJoin : postJoins_) {
    if (!postJoin || !postJoin->chain) continue;
    const float mono = (outputLeft + outputRight) * 0.5f;
    const StereoSample processed = postJoin->chain->process({mono, mono});
    outputLeft = processed.left;
    outputRight = processed.right;
  }
  return true;
}

std::size_t FlexibleRoutingProgram::laneCount() const noexcept
{
  return graph_ ? graph_->laneCount() : 0;
}

std::size_t FlexibleRoutingProgram::blockSize() const noexcept
{
  return graph_ ? graph_->blockSize() : 0;
}

std::string_view FlexibleRoutingProgram::laneId(std::size_t lane) const noexcept
{
  return graph_ ? graph_->laneId(lane) : std::string_view{};
}

std::size_t FlexibleRoutingProgram::postJoinCount() const noexcept
{
  return graph_ ? graph_->joinStageCount() : 0;
}

std::string_view FlexibleRoutingProgram::postJoinId(std::size_t stage) const noexcept
{
  return graph_ ? graph_->joinStageId(stage) : std::string_view{};
}

bool FlexibleRoutingProgram::parallelEnabled() const noexcept
{
  return graph_ && graph_->parallelEnabled();
}

bool FlexibleRoutingProgram::postJoinParallelEnabled() const noexcept
{
  return graph_ && graph_->postJoinParallelEnabled();
}

bool FlexibleRoutingProgram::workersReady() const noexcept
{
  return graph_ && graph_->workersReady();
}

bool FlexibleRoutingProgram::usedSequentialFallback() const noexcept
{
  return graph_ && graph_->usedSequentialFallback();
}

std::uint64_t FlexibleRoutingProgram::underflowBlockCount() const noexcept
{
  return graph_ ? graph_->underflowBlockCount() : 0;
}

std::uint64_t FlexibleRoutingProgram::submissionMissCount() const noexcept
{
  return graph_ ? graph_->submissionMissCount() : 0;
}

std::uint64_t FlexibleRoutingProgram::directWaitOverBudgetCount() const noexcept
{
  return graph_ ? graph_->directWaitOverBudgetCount() : 0;
}

std::uint64_t FlexibleRoutingProgram::directWaitNanoseconds() const noexcept
{
  return graph_ ? graph_->directWaitNanoseconds() : 0;
}

std::uint64_t FlexibleRoutingProgram::directWaitMaximumNanoseconds() const noexcept
{
  return graph_ ? graph_->directWaitMaximumNanoseconds() : 0;
}

ParallelLaneTimingSnapshot FlexibleRoutingProgram::laneTiming(std::size_t lane) const noexcept
{
  return graph_ ? graph_->laneTiming(lane) : ParallelLaneTimingSnapshot{};
}

std::uint64_t FlexibleRoutingProgram::nonFiniteBlockCount() const noexcept
{
  std::uint64_t count = 0;
  for (const auto& lane : lanes_) {
    if (lane && lane->chain) count += lane->chain->nonFiniteBlockCount();
  }
  for (const auto& postJoin : postJoins_) {
    if (postJoin && postJoin->chain) count += postJoin->chain->nonFiniteBlockCount();
  }
  return count;
}

std::uint64_t FlexibleRoutingProgram::parallelWaitOverBudgetCount() const noexcept
{
  std::uint64_t count = 0;
  for (const auto& lane : lanes_) {
    if (lane && lane->chain) count += lane->chain->parallelWaitOverBudgetCount();
  }
  for (const auto& postJoin : postJoins_) {
    if (postJoin && postJoin->chain) count += postJoin->chain->parallelWaitOverBudgetCount();
  }
  return count;
}

std::uint64_t FlexibleRoutingProgram::postJoinUnderflowBlockCount() const noexcept
{
  return graph_ ? graph_->postJoinUnderflowBlockCount() : 0;
}

std::uint64_t FlexibleRoutingProgram::postJoinSubmissionMissCount() const noexcept
{
  return graph_ ? graph_->postJoinSubmissionMissCount() : 0;
}

ParallelStereoStageTimingSnapshot FlexibleRoutingProgram::postJoinTiming() const noexcept
{
  return graph_ ? graph_->postJoinTiming() : ParallelStereoStageTimingSnapshot{};
}

std::string FlexibleRoutingProgram::firstNonFiniteBlockId() const
{
  for (std::size_t index = 0; index < lanes_.size(); ++index) {
    const auto& lane = lanes_[index];
    if (!lane || !lane->chain) continue;
    const std::string blockId = lane->chain->firstNonFiniteBlockId();
    if (!blockId.empty()) {
      const std::string_view id = laneId(index);
      return id.empty() ? blockId : std::string{id} + "/" + blockId;
    }
  }
  for (std::size_t index = 0; index < postJoins_.size(); ++index) {
    const auto& postJoin = postJoins_[index];
    if (!postJoin || !postJoin->chain) continue;
    const std::string blockId = postJoin->chain->firstNonFiniteBlockId();
    if (!blockId.empty()) {
      const std::string_view id = postJoinId(index);
      return id.empty() ? blockId : std::string{id} + "/" + blockId;
    }
  }
  return {};
}

ClipDiagnosticsSnapshot FlexibleRoutingProgram::takeClipDiagnostics()
{
  ClipDiagnosticsSnapshot diagnostics;
  for (std::size_t index = 0; index < lanes_.size(); ++index) {
    const auto& lane = lanes_[index];
    if (!lane || !lane->chain) continue;
    auto stages = lane->chain->takeClipDiagnostics();
    const std::string prefix{laneId(index)};
    for (auto& stage : stages) {
      stage.id = prefix.empty() ? std::move(stage.id)
                                : prefix + "/" + stage.id;
      diagnostics.stages.push_back(std::move(stage));
    }
  }
  for (std::size_t index = 0; index < postJoins_.size(); ++index) {
    const auto& postJoin = postJoins_[index];
    if (!postJoin || !postJoin->chain) continue;
    auto stages = postJoin->chain->takeClipDiagnostics();
    const std::string prefix{postJoinId(index)};
    for (auto& stage : stages) {
      stage.id = prefix.empty() ? std::move(stage.id)
                                : prefix + "/" + stage.id;
      diagnostics.stages.push_back(std::move(stage));
    }
  }
  return diagnostics;
}

std::size_t FlexibleRoutingProgram::tailFrames() const noexcept
{
  std::size_t tail = 0;
  for (const auto& lane : lanes_) {
    if (lane && lane->chain) tail += lane->chain->tailFrames();
  }
  for (const auto& postJoin : postJoins_) {
    if (postJoin && postJoin->chain) tail += postJoin->chain->tailFrames();
  }
  return tail;
}

void FlexibleRoutingProgram::reset() noexcept
{
  if (graph_) graph_->reset();
}

void FlexibleRoutingProgram::processLane(void* opaque, const float* input,
                                          float* output, std::size_t frames) noexcept
{
  auto* context = static_cast<LaneContext*>(opaque);
  if (!context || !context->chain || !input || !output
      || frames > context->left.size() || frames > context->right.size()) {
    if (output) std::fill(output, output + frames, 0.0f);
    return;
  }

  context->chain->processBlock(input, context->left.data(), context->right.data(), frames);
  switch (context->output) {
  case FlexibleRoutingLaneOutput::Left:
    std::copy(context->left.begin(),
              context->left.begin() + static_cast<std::ptrdiff_t>(frames), output);
    break;
  case FlexibleRoutingLaneOutput::Right:
    std::copy(context->right.begin(),
              context->right.begin() + static_cast<std::ptrdiff_t>(frames), output);
    break;
  case FlexibleRoutingLaneOutput::Downmix:
    for (std::size_t i = 0; i < frames; ++i) {
      output[i] = (context->left[i] + context->right[i]) * 0.5f;
    }
    break;
  }
}

void FlexibleRoutingProgram::resetLane(void* opaque) noexcept
{
  auto* context = static_cast<LaneContext*>(opaque);
  if (context && context->chain) context->chain->reset();
}

void FlexibleRoutingProgram::processPostJoin(void* opaque, float* left,
                                              float* right, std::size_t frames) noexcept
{
  auto* context = static_cast<PostJoinContext*>(opaque);
  if (!context || !context->chain || !left || !right
      || frames > context->mono.size()) {
    if (left) std::fill(left, left + frames, 0.0f);
    if (right) std::fill(right, right + frames, 0.0f);
    return;
  }

  for (std::size_t i = 0; i < frames; ++i) {
    context->mono[i] = (left[i] + right[i]) * 0.5f;
  }
  context->chain->processBlock(context->mono.data(), left, right, frames);
}

void FlexibleRoutingProgram::resetPostJoin(void* opaque) noexcept
{
  auto* context = static_cast<PostJoinContext*>(opaque);
  if (context && context->chain) context->chain->reset();
}

} // namespace ardor
