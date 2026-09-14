#pragma once

#include "dsp/FlexibleRoutingGraph.h"
#include "dsp/RuntimeChain.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ardor {

// RuntimeChain is stereo internally, while the parallel graph deliberately
// exposes mono lane contributions to make pan and wet/dry/wet mixing explicit.
// This selects how a lane's prepared stereo result becomes that contribution.
enum class FlexibleRoutingLaneOutput {
  Downmix,
  Left,
  Right,
};

struct FlexibleRoutingProgramLane {
  std::string id;
  std::unique_ptr<RuntimeChain> chain;
  ParallelLaneMixConfig mix{};
  int workerCpu = -1;
  FlexibleRoutingLaneOutput output = FlexibleRoutingLaneOutput::Downmix;

  // This is planner metadata, not a RuntimeChain introspection shortcut.  The
  // control-thread loader sets it when a lane contains a convolution block so
  // admission can reject a topology known to be unsafe for pipelining.
  bool hasPerLaneConvolution = false;
};

// A post-join RuntimeChain receives the mixed signal as mono and writes a
// stereo result. This is intentionally constrained to mono-compatible stages
// (most importantly a shared cabinet/IR); arbitrary stereo DAG semantics are
// not implied by this first prepared-program boundary.
struct FlexibleRoutingProgramPostJoin {
  std::string id;
  std::unique_ptr<RuntimeChain> chain;
};

struct FlexibleRoutingAdmission {
  // Zero means no policy limit. A host can set a finite limit before exposing
  // a routing editor so memory use is bounded by an explicit product choice.
  std::size_t maxLanes = 0;

  // A multi-lane graph must not silently become a serial graph when the
  // caller is relying on parallel workers to meet its block deadline.
  bool requireParallelForMultipleLanes = true;

  // Pi measurements show that pipelining independent long convolvers is not
  // a generally safe admission. The initial admitted topology is direct mode
  // for expensive post-processing, with pipelining enabled only when every
  // configured stage has an explicit bounded-cost policy.
  bool rejectPipelinedPerLaneConvolution = true;

  // Keep serial post-join stages out of a pipelined lane graph unless the
  // caller explicitly selects the dedicated post-stage worker below.
  bool rejectPipelinedPostJoin = true;
};

struct FlexibleRoutingProgramOptions {
  ParallelLaneExecutorOptions executor{};
  ParallelStereoStageExecutorOptions postJoinExecutor{};
  ParallelLaneMixConfig dry{};
  ParallelLaneUnderflowPolicy underflowPolicy =
    ParallelLaneUnderflowPolicy::DryFallback;
  FlexibleRoutingAdmission admission{};
};

// Control-thread-owned prepared program for independent RuntimeChain lanes.
// prepare() performs all allocations and worker setup. processBlock() only
// dispatches already prepared buffers and is safe for the realtime path after
// a successful prepare().
class FlexibleRoutingProgram {
public:
  FlexibleRoutingProgram() = default;
  ~FlexibleRoutingProgram() = default;

  FlexibleRoutingProgram(const FlexibleRoutingProgram&) = delete;
  FlexibleRoutingProgram& operator=(const FlexibleRoutingProgram&) = delete;
  FlexibleRoutingProgram(FlexibleRoutingProgram&&) = delete;
  FlexibleRoutingProgram& operator=(FlexibleRoutingProgram&&) = delete;

  // A program is an immutable activation snapshot. Build a new instance for
  // replacement instead of reconfiguring one that may already be live.
  bool prepare(std::vector<FlexibleRoutingProgramLane> lanes,
               FlexibleRoutingProgramOptions options,
               std::string& error);

  bool prepare(std::vector<FlexibleRoutingProgramLane> lanes,
               std::vector<FlexibleRoutingProgramPostJoin> postJoins,
               FlexibleRoutingProgramOptions options,
               std::string& error);

  // If postJoinExecutor.mode is Pipelined, the post-join result has explicit
  // bounded block latency and may report postJoinReady == false during warmup.
  bool processBlock(const float* input, float* outputLeft, float* outputRight,
                    std::size_t frames,
                    FlexibleRoutingGraphProcessResult& result) noexcept;

  // Compatibility path for PedalEngine's existing one-sample API. It is a
  // serial lane evaluation by design; realtime block callers should use
  // processBlock() so the configured worker topology is preserved. It returns
  // false for a pipelined post-join program because that contract is block-only.
  bool processSample(float input, float& outputLeft, float& outputRight) noexcept;

  bool prepared() const noexcept { return prepared_; }
  std::size_t laneCount() const noexcept;
  std::size_t blockSize() const noexcept;
  std::string_view laneId(std::size_t lane) const noexcept;
  std::size_t postJoinCount() const noexcept;
  std::string_view postJoinId(std::size_t stage) const noexcept;
  bool parallelEnabled() const noexcept;
  bool postJoinParallelEnabled() const noexcept;
  bool workersReady() const noexcept;
  bool usedSequentialFallback() const noexcept;
  std::uint64_t underflowBlockCount() const noexcept;
  std::uint64_t submissionMissCount() const noexcept;
  std::uint64_t directWaitOverBudgetCount() const noexcept;
  std::uint64_t directWaitNanoseconds() const noexcept;
  std::uint64_t directWaitMaximumNanoseconds() const noexcept;
  ParallelLaneTimingSnapshot laneTiming(std::size_t lane) const noexcept;
  std::uint64_t nonFiniteBlockCount() const noexcept;
  std::uint64_t parallelWaitOverBudgetCount() const noexcept;
  std::uint64_t postJoinUnderflowBlockCount() const noexcept;
  std::uint64_t postJoinSubmissionMissCount() const noexcept;
  ParallelStereoStageTimingSnapshot postJoinTiming() const noexcept;
  std::string firstNonFiniteBlockId() const;
  ClipDiagnosticsSnapshot takeClipDiagnostics();
  std::size_t tailFrames() const noexcept;

  // Resets worker generations and every owned RuntimeChain. This is a
  // control-thread lifecycle operation and waits for any in-flight worker.
  void reset() noexcept;

private:
  struct LaneContext {
    std::unique_ptr<RuntimeChain> chain;
    std::vector<float> left;
    std::vector<float> right;
    FlexibleRoutingLaneOutput output = FlexibleRoutingLaneOutput::Downmix;
    float leftGain = 0.0f;
    float rightGain = 0.0f;
    bool enabled = true;
  };

  struct PostJoinContext {
    std::unique_ptr<RuntimeChain> chain;
    std::vector<float> mono;
  };

  static void processLane(void* context, const float* input, float* output,
                          std::size_t frames) noexcept;
  static void resetLane(void* context) noexcept;
  static void processPostJoin(void* context, float* left, float* right,
                              std::size_t frames) noexcept;
  static void resetPostJoin(void* context) noexcept;

  std::vector<std::unique_ptr<LaneContext>> lanes_;
  std::vector<std::unique_ptr<PostJoinContext>> postJoins_;
  // Keep the graph after lane contexts so its worker threads are stopped before
  // those callback targets are destroyed during program teardown.
  std::unique_ptr<FlexibleRoutingGraph> graph_;
  float dryLeftGain_ = 0.0f;
  float dryRightGain_ = 0.0f;
  bool prepared_ = false;
};

} // namespace ardor
