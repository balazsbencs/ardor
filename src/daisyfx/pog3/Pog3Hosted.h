#pragma once

#include "daisyfx/pog3/Pog3Processor.h"
#include "daisyfx/pog3/Pog3Recovery.h"
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
  RecoverySnapshot recovery() const noexcept {
    return {misses_.load(std::memory_order_relaxed), recoveries_.load(std::memory_order_relaxed),
            recoveryState_.load(std::memory_order_acquire)};
  }
private:
  static void work(void* context, float* left, float* right, std::size_t frames) noexcept;
  void apply(const Values& values) noexcept;
  void fail(bool retryAllowed) noexcept;
  void rememberDry(const float* left, const float* right, std::size_t frames) noexcept;
  void fallback(float* left, float* right, std::size_t frames) noexcept;
  void rememberOutput(const float* left, const float* right, std::size_t frames) noexcept;
  Pog3Processor core_;
  ParameterTargets requested_;
  std::vector<Values> snapshots_;
  std::size_t frames_ = 0, delayBlocks_ = 0, remaining_ = 0;
  std::uint64_t submitted_ = 0, workerGeneration_ = 0;
  // Published to the worker by the next input-slot/semaphore handoff only
  // after every old job has completed. No executor generation is rewound.
  std::uint64_t restartGeneration_ = 1;
  std::atomic<std::uint64_t> misses_{0};
  std::atomic<std::uint64_t> recoveries_{0};
  std::atomic<RecoveryState> recoveryState_{RecoveryState::Healthy};
  static constexpr std::size_t kDelaySamples = 256;
  static constexpr std::size_t kBypassFadeSamples = 128;
  static constexpr std::size_t kRecoveryFadeSamples = 960;
  std::array<float, kDelaySamples> dryLeft_{}, dryRight_{};
  std::array<float, 128> dryBlockLeft_{}, dryBlockRight_{};
  std::size_t dryCursor_ = 0, bypassFade_ = 0, recoveryFade_ = 0;
  float lastLeft_ = 0, lastRight_ = 0, holdLeft_ = 0, holdRight_ = 0;
  bool retryUsed_ = false;
  bool parallel_ = false, failed_ = false;
  // Join before destroying the callback context, snapshots and core.
  ParallelStereoStageExecutor executor_;
};
} // namespace ardor::pog3
