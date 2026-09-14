#pragma once

#include "dsp/ParallelLaneExecutor.h"
#include "dsp/ParallelLaneMixer.h"
#include "dsp/ParallelStereoStageExecutor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ardor {

// A lane callback owns the sequential node chain for one branch. It is
// configured on the control thread and called with one complete mono block on
// the audio thread or on its assigned worker.
using FlexibleRoutingLaneProcess = ParallelLaneProcess;
using FlexibleRoutingReset = void (*)(void* context);

// Join stages run in order after lane mixing. They operate in-place on the
// graph's preallocated stereo buffers and must not allocate or retain pointers
// after the callback returns.
using FlexibleRoutingJoinProcess = void (*)(void* context, float* left,
                                             float* right, std::size_t frames);

struct FlexibleRoutingLane {
  std::string id;
  void* context = nullptr;
  FlexibleRoutingLaneProcess process = nullptr;
  int workerCpu = -1;
  ParallelLaneMixConfig mix{};
  FlexibleRoutingReset reset = nullptr;
};

struct FlexibleRoutingJoinStage {
  std::string id;
  void* context = nullptr;
  FlexibleRoutingJoinProcess process = nullptr;
  FlexibleRoutingReset reset = nullptr;
};

struct FlexibleRoutingGraphOptions {
  ParallelLaneExecutorOptions executor{};
  ParallelLaneMixConfig dry{};
  ParallelLaneUnderflowPolicy underflowPolicy =
    ParallelLaneUnderflowPolicy::DryFallback;
  // Serial post-join stages are direct by default. Pipelined mode moves the
  // complete ordered stage list to a dedicated worker and adds explicit
  // bounded latency; it never waits on the audio callback.
  ParallelStereoStageExecutorOptions postJoinExecutor{};
};

struct FlexibleRoutingGraphProcessResult {
  bool accepted = false;
  bool wetReady = false;
  bool usedFallback = false;
  bool workersReady = false;
  bool usedSequentialFallback = false;
  bool postJoinAccepted = true;
  bool postJoinReady = true;
  bool usedPostJoinFallback = false;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
  std::uint64_t postJoinSubmittedGeneration = 0;
  std::uint64_t postJoinOutputGeneration = 0;
};

// DSP-only runtime graph boundary for a set of independent mono lanes and
// serial post-join stages. The lane and stage descriptors are immutable after
// configure(); all realtime buffers are prepared up front. The
// FlexibleRoutingProgram owner is the production-facing adapter for
// RuntimeChain lanes; this generic boundary remains useful for benchmark and
// custom join-stage probes.
class FlexibleRoutingGraph {
public:
  FlexibleRoutingGraph() = default;
  ~FlexibleRoutingGraph() = default;

  FlexibleRoutingGraph(const FlexibleRoutingGraph&) = delete;
  FlexibleRoutingGraph& operator=(const FlexibleRoutingGraph&) = delete;
  FlexibleRoutingGraph(FlexibleRoutingGraph&&) = delete;
  FlexibleRoutingGraph& operator=(FlexibleRoutingGraph&&) = delete;

  bool configure(std::vector<FlexibleRoutingLane> lanes,
                 std::vector<FlexibleRoutingJoinStage> joinStages,
                 FlexibleRoutingGraphOptions options,
                 std::string& error);

  // Processes one complete block without allocation. A pipelined graph may
  // return accepted == false when its bounded executor has no free slot; it
  // still emits the latest available wet block or the configured dry fallback.
  bool processBlock(const float* input, float* outputLeft, float* outputRight,
                    std::size_t frames,
                    FlexibleRoutingGraphProcessResult& result) noexcept;

  std::size_t laneCount() const noexcept { return laneIds_.size(); }
  std::size_t joinStageCount() const noexcept { return joinStages_.size(); }
  std::size_t blockSize() const noexcept { return blockSize_; }
  std::string_view laneId(std::size_t lane) const noexcept
  {
    return lane < laneIds_.size() ? laneIds_[lane] : std::string_view{};
  }
  std::string_view joinStageId(std::size_t stage) const noexcept
  {
    return stage < joinStages_.size() ? joinStages_[stage].id : std::string_view{};
  }
  bool parallelEnabled() const noexcept { return executor_.parallelEnabled(); }
  bool postJoinParallelEnabled() const noexcept
  {
    return postJoinExecutor_.parallelEnabled();
  }
  bool workersReady() const noexcept
  {
    return executor_.workersReady()
      && (joinStages_.empty() || postJoinExecutor_.workersReady());
  }
  bool usedSequentialFallback() const noexcept { return sequentialFallback_; }

  std::uint64_t underflowBlockCount() const noexcept
  {
    return mixer_.underflowBlockCount();
  }

  std::uint64_t submissionMissCount() const noexcept
  {
    return executor_.pipelineSubmissionMissCount();
  }

  std::uint64_t directWaitOverBudgetCount() const noexcept
  {
    return executor_.directWaitOverBudgetCount();
  }

  std::uint64_t directWaitNanoseconds() const noexcept
  {
    return executor_.directWaitNanoseconds();
  }

  std::uint64_t directWaitMaximumNanoseconds() const noexcept
  {
    return executor_.directWaitMaximumNanoseconds();
  }

  ParallelLaneTimingSnapshot laneTiming(std::size_t lane) const noexcept
  {
    return executor_.timing(lane);
  }

  std::uint64_t postJoinUnderflowBlockCount() const noexcept
  {
    return postJoinExecutor_.underflowCount();
  }

  std::uint64_t postJoinSubmissionMissCount() const noexcept
  {
    return postJoinExecutor_.submissionMissCount();
  }

  ParallelStereoStageTimingSnapshot postJoinTiming() const noexcept
  {
    return postJoinExecutor_.timing();
  }

  void reset() noexcept;

private:
  struct ResetTarget {
    void* context = nullptr;
    FlexibleRoutingReset reset = nullptr;
  };

  static void processJoinStages(void* context, float* left, float* right,
                                std::size_t frames) noexcept;

  std::vector<std::string> laneIds_;
  std::vector<ResetTarget> laneResets_;
  std::vector<FlexibleRoutingJoinStage> joinStages_;
  std::vector<const float*> laneInputs_;
  std::vector<float> mixedLeft_;
  std::vector<float> mixedRight_;
  std::size_t blockSize_ = 0;
  bool configured_ = false;
  bool sequentialFallback_ = false;
  ParallelLaneExecutor executor_;
  ParallelLaneMixer mixer_;
  ParallelStereoStageExecutor postJoinExecutor_;
};

} // namespace ardor
