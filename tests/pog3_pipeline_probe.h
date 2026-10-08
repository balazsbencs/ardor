#pragma once

// Diagnostic adapter only: require exactly generation n-delay at callback n.
// Never wait, replay stale audio, or bypass POG3 on a callback deadline miss.
#include "daisyfx/pog3/Pog3Processor.h"
#include "dsp/DenormalGuard.h"
#include "dsp/ParallelStereoStageExecutor.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <sched.h>
#include <thread>
#include <time.h>
#include <vector>

extern "C" void pog3_malloc_begin() __attribute__((weak));
extern "C" void pog3_malloc_end(std::size_t*, std::size_t*) __attribute__((weak));

namespace pog3_probe {
inline std::uint64_t clockNs(clockid_t clock = CLOCK_MONOTONIC) noexcept {
  timespec time{};
  clock_gettime(clock, &time);
  return static_cast<std::uint64_t>(time.tv_sec) * 1000000000ULL + time.tv_nsec;
}
inline void apply(ardor::pog3::Pog3Processor& core,
                  const ardor::pog3::Values& targets) noexcept {
  for (std::size_t i = 0; i < targets.size(); ++i) core.setParameterTarget(i, targets[i]);
}
struct Job {
  std::uint64_t submittedNs = 0, startNs = 0, endNs = 0, cpuNs = 0;
  unsigned transforms = 0;
  int cpu = -1;
  std::size_t allocations = 0, releases = 0;
};

class Worker {
public:
  Worker(ardor::pog3::Pog3Processor& core, std::size_t capacity)
    : core_(core), jobs_(capacity + 1) {}
  // Lifecycle/diagnostic operations below belong to the control thread.
  bool configure(std::size_t frames, int cpu, bool realtime, std::string& error,
                  unsigned delayBlocks = 1) {
    if (delayBlocks < 1 || delayBlocks > snapshots_.size()) {
      error = "diagnostic supports one or two delay blocks"; return false;
    }
    delayBlocks_ = delayBlocks;
    ardor::ParallelStereoStageExecutorOptions options;
    options.blockSize = frames;
    options.sampleRate = 48000;
    options.mode = ardor::ParallelStereoStageExecutionMode::Pipelined;
    options.pipelineSlots = 2;
    options.fixedOutputDelayBlocks = delayBlocks;
    options.workerCpu = cpu;
    options.workerPriority = 69;
    options.requireRealtimeScheduling = realtime;
    options.requireAffinity = cpu >= 0;
    return executor_.configure(process, this, options, error);
  }
  bool processBlock(const float* left, const float* right, float* outLeft,
                    float* outRight, std::size_t frames,
                    const ardor::pog3::Values& targets) noexcept {
    // If the required output has not completed, do not recycle snapshots or
    // enqueue more input. Stop the experiment with the exact miss recorded.
    const auto generation = submitted_ + 1;
    const auto expected = generation > delayBlocks_ ? generation - delayBlocks_ : 0;
    observedCompleted_ = executor_.completedGeneration();
    if (observedCompleted_ < expected) { ++late_; return false; }
    if (generation >= jobs_.size()) { ++capacityMisses_; return false; }
    auto& snapshot = snapshots_[generation % snapshots_.size()];
    snapshot.targets = targets;
    snapshot.submittedNs = clockNs();
    // Required completion plus the executor semaphore release/acquire makes
    // the two snapshot slots safe without sharing the processor's live state.
    ardor::ParallelStereoStageProcessResult result;
    if (!executor_.processBlock(left, right, outLeft, outRight, frames, result)) {
      ++submissionMisses_; return false;
    }
    if (!result.accepted || result.submittedGeneration != generation) {
      ++submissionMisses_; return false;
    }
    submitted_ = generation;
    if (expected == 0) {
      if (result.outputReady || !result.usedFallback) { ++wrongOutput_; return false; }
      // Explicit startup silence: the generic executor's initial bypass is
      // inappropriate for a fixed-latency effect.
      std::fill_n(outLeft, frames, 0.0f);
      std::fill_n(outRight, frames, 0.0f);
    } else if (!result.outputReady || result.usedFallback
               || result.outputGeneration != expected) {
      ++wrongOutput_; return false;
    } else {
      consumed_ = result.outputGeneration;
    }
    return true;
  }
  bool waitForIdle() const noexcept {
    const auto deadline = clockNs() + 5000000000ULL;
    while (executor_.completedGeneration() < submitted_) {
      if (clockNs() >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
  }
  void reset() noexcept {
    executor_.reset();
    core_.reset();
    submitted_ = consumed_ = workerGeneration_ = observedCompleted_ = 0;
    late_ = submissionMisses_ = wrongOutput_ = capacityMisses_ = 0;
    std::fill(jobs_.begin(), jobs_.end(), Job{});
  }
  void clear() noexcept { executor_.clear(); }
  std::uint64_t submitted() const noexcept { return submitted_; }
  std::uint64_t consumed() const noexcept { return consumed_; }
  std::uint64_t completed() const noexcept { return executor_.completedGeneration(); }
  std::uint64_t late() const noexcept { return late_; }
  std::uint64_t submissionMisses() const noexcept { return submissionMisses_; }
  std::uint64_t wrongOutput() const noexcept { return wrongOutput_; }
  std::uint64_t capacityMisses() const noexcept { return capacityMisses_; }
  std::uint64_t observedCompleted() const noexcept { return observedCompleted_; }
  std::uint64_t expected() const noexcept {
    return submitted_ + 1 > delayBlocks_ ? submitted_ + 1 - delayBlocks_ : 0;
  }
  const std::vector<Job>& jobs() const noexcept { return jobs_; }
  bool monitorAllocations = false;
  // Offline test gate only; null in hardware runs. Release it before shutdown.
  std::atomic<bool>* testHold = nullptr;
private:
  struct Snapshot { ardor::pog3::Values targets{}; std::uint64_t submittedNs = 0; };
  static void process(void* context, float* left, float* right, std::size_t frames) noexcept {
    auto& self = *static_cast<Worker*>(context);
    if (self.testHold)
      while (self.testHold->load(std::memory_order_acquire)) std::this_thread::yield();
    const auto generation = ++self.workerGeneration_;
    const auto& snapshot = self.snapshots_[generation % self.snapshots_.size()];
    auto& job = self.jobs_[generation];
    if (self.monitorAllocations) pog3_malloc_begin();
    job.submittedNs = snapshot.submittedNs;
    job.startNs = clockNs();
    const auto cpuStart = clockNs(CLOCK_THREAD_CPUTIME_ID);
    const auto transforms = self.core_.transformCount();
    {
      ardor::ScopedDenormalGuard guard;
      apply(self.core_, snapshot.targets);
      for (std::size_t i = 0; i < frames; ++i) {
        const auto output = self.core_.process({left[i], right[i]}).mixed;
        left[i] = output.left;
        right[i] = output.right;
      }
    }
    job.cpuNs = clockNs(CLOCK_THREAD_CPUTIME_ID) - cpuStart;
    job.endNs = clockNs();
    job.transforms = static_cast<unsigned>(self.core_.transformCount() - transforms);
    job.cpu = sched_getcpu();
    if (self.monitorAllocations) pog3_malloc_end(&job.allocations, &job.releases);
    // The executor publishes completion after returning, ordering this trace
    // with its output buffers for the next callback and post-run analysis.
  }
  ardor::pog3::Pog3Processor& core_;
  std::vector<Job> jobs_;
  std::array<Snapshot, 2> snapshots_{};
  std::uint64_t submitted_ = 0, consumed_ = 0, workerGeneration_ = 0;
  std::uint64_t observedCompleted_ = 0;
  std::uint64_t late_ = 0, submissionMisses_ = 0, wrongOutput_ = 0, capacityMisses_ = 0;
  unsigned delayBlocks_ = 1;
  // Destroy/join the executor before any worker context storage or the core.
  ardor::ParallelStereoStageExecutor executor_;
};
} // namespace pog3_probe
