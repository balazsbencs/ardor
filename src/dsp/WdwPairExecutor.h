#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ardor {

// One complete lane callback.  The pair executor supplies the same mono
// input generation to both callbacks and gives each lane private stereo output
// storage.  Callbacks run either on the caller (Direct) or on their dedicated
// worker (Pipelined); they never allocate or synchronize with the other lane.
using WdwPairLaneProcess = void (*)(void* context, const float* input,
                                    float* outputLeft, float* outputRight,
                                    std::size_t frames);

struct WdwPairLane {
  void* context = nullptr;
  WdwPairLaneProcess process = nullptr;
  int workerCpu = -1;
};

enum class WdwPairExecutionMode {
  Direct,
  Pipelined,
};

struct WdwPairExecutorOptions {
  std::size_t blockSize = 0;
  double sampleRate = 0.0;
  WdwPairExecutionMode mode = WdwPairExecutionMode::Direct;
  // Three slots absorb one scheduling jitter quantum on the Pi without
  // materially changing the fixed audio latency; two remains a valid
  // explicitly selected low-memory configuration.
  std::size_t pipelineSlots = 3;
  int workerPriority = 69;

  // Production callers should keep these true.  Tests and tools can disable
  // the privilege checks while retaining the same worker topology.
  bool requireWorkerSetup = true;
  bool requireRealtimeScheduling = true;
  bool requireAffinity = true;
  bool collectTiming = false;

  // Number of callback quanta for which a previously committed pair may be
  // held when no newer complete pair is ready.  Zero means emit silence on an
  // underflow; it never means bypass the raw input.
  std::size_t maxHoldBlocks = 1;
};

struct WdwPairTimingSnapshot {
  std::uint64_t calls = 0;
  std::uint64_t totalNanoseconds = 0;
  std::uint64_t maximumNanoseconds = 0;
  int requestedCpu = -1;
  int actualCpu = -1;
};

struct WdwPairProcessResult {
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

// Bounded executor for exactly two complete stereo lanes.  The pipelined mode
// owns one generation ring shared by both workers, so a callback can publish
// only a dry/wet pair produced from the same input generation.  It never waits
// for a worker in processBlock().
class WdwPairExecutor {
public:
  WdwPairExecutor();
  ~WdwPairExecutor();

  WdwPairExecutor(const WdwPairExecutor&) = delete;
  WdwPairExecutor& operator=(const WdwPairExecutor&) = delete;
  WdwPairExecutor(WdwPairExecutor&&) = delete;
  WdwPairExecutor& operator=(WdwPairExecutor&&) = delete;

  bool configure(WdwPairLane dry, WdwPairLane wet,
                 WdwPairExecutorOptions options, std::string& error);

  // A valid pipelined call may return accepted == false when the bounded ring
  // is full.  The output is then the last complete pair or bounded silence.
  bool processBlock(const float* input, float* dryLeft, float* dryRight,
                    float* wetLeft, float* wetRight, std::size_t frames,
                    WdwPairProcessResult& result);

  bool configured() const noexcept;
  bool parallelEnabled() const noexcept;
  bool workersReady() const noexcept;
  std::size_t blockSize() const noexcept;
  std::uint64_t underflowCount() const noexcept;
  std::uint64_t submissionMissCount() const noexcept;
  WdwPairTimingSnapshot timing(std::size_t lane) const noexcept;

  // Control-thread lifecycle operations. reset() waits for submitted worker
  // generations and clears the bounded ring; clear() also stops the workers.
  void reset() noexcept;
  void clear() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ardor
