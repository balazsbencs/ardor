#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ardor {

// The callback is configured off the audio thread and called with a complete
// mono block.  `output` is owned by ParallelLaneExecutor and is already sized
// to the configured block size.
using ParallelLaneProcess = void (*)(void* context, const float* input,
                                     float* output, std::size_t frames);

struct ParallelLane {
  void* context = nullptr;
  ParallelLaneProcess process = nullptr;
  // CPU requested for lanes after lane zero.  A negative value leaves the
  // worker unpinned when affinity is not required.
  int workerCpu = -1;
};

enum class ParallelLaneExecutionMode {
  Direct,
  Pipelined,
};

struct ParallelLaneExecutorOptions {
  std::size_t blockSize = 0;
  double sampleRate = 0.0;
  ParallelLaneExecutionMode mode = ParallelLaneExecutionMode::Direct;
  std::size_t pipelineSlots = 2;
  int workerPriority = 69;

  // Production callers should keep these true.  Tests and tools can opt into
  // a sequential fallback when the host cannot grant SCHED_FIFO/affinity.
  bool requireWorkerSetup = true;
  bool requireRealtimeScheduling = true;
  bool requireAffinity = true;

  // Optional diagnostics. When disabled, the realtime path does not take
  // per-stage timestamps; counters remain allocation-free in either mode.
  bool collectTiming = false;
};

struct ParallelLaneTimingSnapshot {
  std::uint64_t calls = 0;
  std::uint64_t totalNanoseconds = 0;
  std::uint64_t maximumNanoseconds = 0;
  int requestedCpu = -1;
  int actualCpu = -1;
};

struct ParallelLaneProcessResult {
  bool accepted = false;
  bool outputReady = false;
  bool workersReady = false;
  bool usedSequentialFallback = false;
  std::uint64_t submittedGeneration = 0;
  std::uint64_t outputGeneration = 0;
};

// A bounded executor for a fixed set of independent mono lanes.
//
// Lane zero is run by the caller's thread.  In direct mode all worker lanes
// are submitted for the current block and the call waits for all of them. In
// pipelined mode a block is submitted to a preallocated ring and the output
// from the previous accepted block is exposed through laneOutput().  The
// executor never allocates in processBlock(); if a ring slot is still in use,
// the submission is rejected and the result reports accepted == false.
class ParallelLaneExecutor {
public:
  ParallelLaneExecutor();
  ~ParallelLaneExecutor();

  ParallelLaneExecutor(const ParallelLaneExecutor&) = delete;
  ParallelLaneExecutor& operator=(const ParallelLaneExecutor&) = delete;
  ParallelLaneExecutor(ParallelLaneExecutor&&) = delete;
  ParallelLaneExecutor& operator=(ParallelLaneExecutor&&) = delete;

  bool configure(std::vector<ParallelLane> lanes,
                 ParallelLaneExecutorOptions options,
                 std::string& error);

  // Returns false only for an invalid call (for example, a block-size
  // mismatch).  A valid pipelined call may return true with accepted == false
  // when no ring slot is available.
  bool processBlock(const float* input, std::size_t frames,
                    ParallelLaneProcessResult& result);

  // The returned pointer remains valid until the next call that reuses that
  // pipeline slot or until configure()/reset().  It is null before the first
  // pipelined output is ready.
  const float* laneOutput(std::size_t lane) const noexcept;

  // Pipelined callers that read laneOutput() directly must call this after
  // copying/mixing all lanes. It releases the publication slot for worker
  // reuse; direct and sequential-fallback modes ignore the call.
  void acknowledgeOutput() noexcept;

  std::size_t laneCount() const noexcept;
  std::size_t blockSize() const noexcept;
  bool parallelEnabled() const noexcept;
  bool workersReady() const noexcept;

  std::uint64_t pipelineUnderflowCount() const noexcept;
  std::uint64_t pipelineSubmissionMissCount() const noexcept;
  std::uint64_t directWaitOverBudgetCount() const noexcept;
  std::uint64_t directWaitNanoseconds() const noexcept;
  std::uint64_t directWaitMaximumNanoseconds() const noexcept;
  ParallelLaneTimingSnapshot timing(std::size_t lane) const noexcept;

  // Clears generations, buffers, and counters without changing worker
  // configuration. It may wait for an in-flight worker block to finish.
  void reset() noexcept;

  // Stops worker threads and drops the prepared topology. This is a
  // control-thread lifecycle operation; callers must not invoke it while the
  // audio callback is using processBlock().
  void clear() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ardor
