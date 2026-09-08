#pragma once

#include "dsp/ClipDiagnostics.h"
#include "dsp/RuntimeChain.h"
#include "dsp/WdwPairExecutor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ardor {

// The two complete chains are intentionally explicit.  The dry chain is
// rendered as a mono contribution at the pair boundary; the wet chain keeps
// its stereo result all the way to the final mixer.
struct WdwRoutingLane {
  std::string id;
  std::unique_ptr<RuntimeChain> chain;
  int workerCpu = -1;
};

struct WdwMixConfig {
  float dryLevel = 1.0f;
  float dryPan = 0.0f;       // -1 = left, 0 = centre, +1 = right.
  bool dryEnabled = true;
  float wetLevel = 1.0f;
  float wetWidth = 1.0f;     // 0 = mono, 1 = unchanged stereo.
  bool wetEnabled = true;
};

struct WdwRoutingProgramOptions {
  WdwPairExecutorOptions executor{};
  int audioCpu = -1;
  WdwMixConfig mix{};

  // Declared first-arrival latencies from the prepared lane plans.  The
  // program inserts the smaller path's fixed alignment delay before mixing.
  std::size_t dryLatencyFrames = 0;
  std::size_t wetLatencyFrames = 0;
};

struct WdwRoutingProcessResult {
  bool accepted = false;
  bool outputReady = false;
  bool pairReady = false;
  bool workersReady = false;
  bool usedFallback = false;
  bool heldLastPair = false;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
  std::size_t outputAgeBlocks = 0;
};

// Prepared production-shaped boundary for the fixed two-lane WDW topology.
// Structural changes require a new instance; processBlock() only dispatches
// preallocated buffers and applies the final stereo mix.
class WdwRoutingProgram {
public:
  WdwRoutingProgram() = default;
  ~WdwRoutingProgram() = default;

  WdwRoutingProgram(const WdwRoutingProgram&) = delete;
  WdwRoutingProgram& operator=(const WdwRoutingProgram&) = delete;
  WdwRoutingProgram(WdwRoutingProgram&&) = delete;
  WdwRoutingProgram& operator=(WdwRoutingProgram&&) = delete;

  bool prepare(WdwRoutingLane dry, WdwRoutingLane wet,
               WdwRoutingProgramOptions options, std::string& error);

  bool processBlock(const float* input, float* outputLeft, float* outputRight,
                    std::size_t frames, WdwRoutingProcessResult& result) noexcept;

  // Compatibility path for PedalEngine's one-sample API.  It is only valid
  // for a direct program; a pipelined program is block-quantized and returns
  // false rather than touching worker-owned state from the caller thread.
  bool processSample(float input, float& outputLeft, float& outputRight) noexcept;

  // Control-thread operations.  reset() waits for the pair workers before
  // resetting either RuntimeChain and the alignment state.
  void reset() noexcept;

  // Mix changes are target updates; processBlock() smooths them at block
  // rate.  Structural lane/chain changes still require a new program.
  bool setMix(WdwMixConfig config) noexcept;

  bool prepared() const noexcept { return prepared_; }
  std::size_t blockSize() const noexcept { return blockSize_; }
  double sampleRate() const noexcept { return sampleRate_; }
  std::size_t latencyFrames() const noexcept { return latencyFrames_; }
  std::size_t alignmentDelayFrames(std::size_t lane) const noexcept;
  bool parallelEnabled() const noexcept { return executor_.parallelEnabled(); }
  bool workersReady() const noexcept { return executor_.workersReady(); }
  std::uint64_t pairUnderflowCount() const noexcept
  {
    return executor_.underflowCount();
  }
  std::uint64_t pairSubmissionMissCount() const noexcept
  {
    return executor_.submissionMissCount();
  }
  WdwPairTimingSnapshot laneTiming(std::size_t lane) const noexcept
  {
    return executor_.timing(lane);
  }
  std::uint64_t nonFiniteBlockCount() const noexcept
  {
    return nonFiniteBlocks_.load(std::memory_order_relaxed)
      + (dryContext_ && dryContext_->chain ? dryContext_->chain->nonFiniteBlockCount() : 0)
      + (wetContext_ && wetContext_->chain ? wetContext_->chain->nonFiniteBlockCount() : 0);
  }
  std::uint64_t parallelWaitOverBudgetCount() const noexcept
  {
    const std::uint64_t dry = dryContext_ && dryContext_->chain
      ? dryContext_->chain->parallelWaitOverBudgetCount() : 0;
    const std::uint64_t wet = wetContext_ && wetContext_->chain
      ? wetContext_->chain->parallelWaitOverBudgetCount() : 0;
    return dry + wet;
  }
  std::uint64_t dryFaultBlockCount() const noexcept
  {
    return dryContext_ && dryContext_->chain
      ? dryContext_->chain->nonFiniteBlockCount() : 0;
  }
  std::uint64_t wetFaultBlockCount() const noexcept
  {
    return wetContext_ && wetContext_->chain
      ? wetContext_->chain->nonFiniteBlockCount() : 0;
  }
  std::string firstNonFiniteBlockId() const;
  ClipDiagnosticsSnapshot takeClipDiagnostics();
  std::size_t tailFrames() const noexcept;

private:
  struct LaneContext {
    std::unique_ptr<RuntimeChain> chain;
    bool monoOutput = false;
  };

  struct DelayLine {
    void prepare(std::size_t delayFrames, std::size_t blockSize);
    void process(const float* input, float* output, std::size_t frames) noexcept;
    void reset() noexcept;
    std::size_t delayFrames = 0;
    std::vector<float> storage;
    std::size_t writeIndex = 0;
  };

  struct MixTargets {
    std::atomic<float> dryLeftGain{0.70710678118f};
    std::atomic<float> dryRightGain{0.70710678118f};
    std::atomic<float> wetLevel{1.0f};
    std::atomic<float> wetWidth{1.0f};
  };

  struct MixCurrent {
    float dryLeftGain = 0.70710678118f;
    float dryRightGain = 0.70710678118f;
    float wetLevel = 1.0f;
    float wetWidth = 1.0f;
  };

  static void processDry(void* context, const float* input, float* left,
                         float* right, std::size_t frames) noexcept;
  static void processWet(void* context, const float* input, float* left,
                         float* right, std::size_t frames) noexcept;
  static float smooth(float current, float target) noexcept;
  static bool normalizeMix(WdwMixConfig config, float& dryLeft, float& dryRight,
                           float& wetLevel, float& wetWidth) noexcept;

  std::unique_ptr<LaneContext> dryContext_;
  std::unique_ptr<LaneContext> wetContext_;
  WdwPairExecutor executor_;
  std::vector<float> dryPairLeft_;
  std::vector<float> dryPairRight_;
  std::vector<float> wetPairLeft_;
  std::vector<float> wetPairRight_;
  std::vector<float> alignedDryLeft_;
  std::vector<float> alignedDryRight_;
  std::vector<float> alignedWetLeft_;
  std::vector<float> alignedWetRight_;
  DelayLine dryDelayLeft_;
  DelayLine dryDelayRight_;
  DelayLine wetDelayLeft_;
  DelayLine wetDelayRight_;
  MixTargets mixTargets_;
  MixCurrent mixCurrent_;
  std::atomic<std::uint64_t> nonFiniteBlocks_{0};
  std::size_t blockSize_ = 0;
  double sampleRate_ = 0.0;
  std::size_t dryAlignmentFrames_ = 0;
  std::size_t wetAlignmentFrames_ = 0;
  std::size_t latencyFrames_ = 0;
  bool prepared_ = false;
};

} // namespace ardor
