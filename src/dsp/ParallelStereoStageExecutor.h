#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ardor {

// A serial stereo stage callback. The callback owns the stage state; the
// executor owns all block buffers and only invokes it with a complete block.
using ParallelStereoStageProcess = void (*)(void* context, float* left,
                                             float* right, std::size_t frames);

enum class ParallelStereoStageExecutionMode {
  Direct,
  Pipelined,
};

enum class ParallelStereoStageUnderflowPolicy {
  // Emit the unprocessed input when no processed generation is available.
  Bypass,
  // Repeat the most recently completed processed block. This is useful for
  // continuity experiments, but can hold stale audio for longer than one
  // quantum if the worker is overloaded.
  HoldLast,
};

struct ParallelStereoStageExecutorOptions {
  std::size_t blockSize = 0;
  double sampleRate = 0.0;
  ParallelStereoStageExecutionMode mode = ParallelStereoStageExecutionMode::Direct;
  std::size_t pipelineSlots = 2;
  int workerCpu = -1;
  int workerPriority = 69;

  // Production callers should keep these true. Tests and tools can disable
  // scheduling/affinity checks when the host cannot grant those privileges.
  bool requireWorkerSetup = true;
  bool requireRealtimeScheduling = true;
  bool requireAffinity = true;
  bool collectTiming = false;
  ParallelStereoStageUnderflowPolicy underflowPolicy =
    ParallelStereoStageUnderflowPolicy::Bypass;
};

struct ParallelStereoStageTimingSnapshot {
  std::uint64_t calls = 0;
  std::uint64_t totalNanoseconds = 0;
  std::uint64_t maximumNanoseconds = 0;
  int requestedCpu = -1;
  int actualCpu = -1;
};

struct ParallelStereoStageProcessResult {
  bool accepted = false;
  bool outputReady = false;
  bool workersReady = false;
  bool usedFallback = false;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
};

// Bounded executor for one serial stereo post-stage pipeline. Direct mode
// invokes the callback on the caller. Pipelined mode adds an explicit bounded
// one-or-more-quantum latency and never waits on the audio callback.
class ParallelStereoStageExecutor {
public:
  ParallelStereoStageExecutor();
  ~ParallelStereoStageExecutor();

  ParallelStereoStageExecutor(const ParallelStereoStageExecutor&) = delete;
  ParallelStereoStageExecutor& operator=(const ParallelStereoStageExecutor&) = delete;
  ParallelStereoStageExecutor(ParallelStereoStageExecutor&&) = delete;
  ParallelStereoStageExecutor& operator=(ParallelStereoStageExecutor&&) = delete;

  bool configure(ParallelStereoStageProcess process, void* context,
                 ParallelStereoStageExecutorOptions options,
                 std::string& error);

  // A valid pipelined call may return true with accepted == false when all
  // ring slots are still in use. The result then contains either a completed
  // output or the configured bounded fallback.
  bool processBlock(const float* inputLeft, const float* inputRight,
                    float* outputLeft, float* outputRight, std::size_t frames,
                    ParallelStereoStageProcessResult& result);

  bool configured() const noexcept;
  bool parallelEnabled() const noexcept;
  bool workersReady() const noexcept;
  std::size_t blockSize() const noexcept;
  std::uint64_t underflowCount() const noexcept;
  std::uint64_t submissionMissCount() const noexcept;
  ParallelStereoStageTimingSnapshot timing() const noexcept;

  // Control-thread lifecycle operations. reset() waits for the current worker
  // generation; clear() additionally stops and drops the worker topology.
  void reset() noexcept;
  void clear() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ardor
