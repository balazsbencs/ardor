#pragma once

#include "daisyfx/pog3/Pog3Processor.h"
#include "dsp/ParallelStereoStageExecutor.h"
#include <atomic>
#include <vector>

namespace ardor::pog3 {

// The host publishes controls separately from the worker's dated input targets.
// Configure/reset/teardown require quiesced callbacks. Only the DSP owner ever
// processes or resets the core; the realtime caller never waits for a job.
class Pog3Hosted {
public:
  bool configure(const nlohmann::json& params, float sampleRate,
                 std::size_t frames, int workerCpu, bool realtime,
                 std::string& error);
  bool setTarget(std::string_view key, float value) noexcept;
  bool setTarget(std::size_t index, float value) noexcept;
  VoiceStageOutput process(PitchStereo input) noexcept;
  void processBlock(const float* left, const float* right, float* outputLeft,
                    float* outputRight, std::size_t frames) noexcept;
  void reset() noexcept;
  std::size_t latencyFrames() const noexcept { return parallel_ ? 256 : 0; }
  std::uint64_t underflows() const noexcept { return misses_.load(std::memory_order_relaxed); }
  bool parallel() const noexcept { return parallel_; }
  std::uint64_t completedGeneration() const noexcept { return executor_.completedGeneration(); }
private:
  static void work(void* context, float* left, float* right, std::size_t frames) noexcept;
  void apply(const Values& values) noexcept;
  void fail(float* left, float* right, std::size_t frames) noexcept;
  Pog3Processor core_;
  ParameterTargets requested_;
  std::vector<Values> snapshots_;
  std::size_t frames_ = 0, delayBlocks_ = 0, remaining_ = 0;
  std::uint64_t submitted_ = 0, workerGeneration_ = 0;
  std::atomic<std::uint64_t> misses_{0};
  bool parallel_ = false, failed_ = false;
  // Join before destroying the callback context, snapshots and core.
  ParallelStereoStageExecutor executor_;
};
} // namespace ardor::pog3
