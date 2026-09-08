#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ardor {

enum class ParallelLaneUnderflowPolicy {
  // Keep the previous wet lane sum, then apply the current dry input. This
  // preserves wet continuity while a worker result is unavailable, at the
  // cost of repeating the previous wet block.
  HoldLastWet,
  // Keep the dry path and temporarily remove the wet lanes.
  DryFallback,
  // Emit silence for the block with no completed wet result.
  Silence,
};

struct ParallelLaneMixConfig {
  float level = 1.0f;
  float pan = 0.0f; // -1 = left, 0 = centre, +1 = right.
  bool enabled = true;
};

// Allocation-free stereo graph mixer for independent mono lanes. Configuration
// and prepare() are control-thread operations; processBlock() is safe for the
// realtime path after preparation.
class ParallelLaneMixer {
public:
  ParallelLaneMixer() = default;

  bool prepare(std::size_t laneCount, std::size_t blockSize);

  std::size_t laneCount() const noexcept { return lanes_.size(); }
  std::size_t blockSize() const noexcept { return blockSize_; }

  bool setLane(std::size_t lane, ParallelLaneMixConfig config) noexcept;
  void setDry(ParallelLaneMixConfig config) noexcept;
  void setUnderflowPolicy(ParallelLaneUnderflowPolicy policy) noexcept
  {
    underflowPolicy_ = policy;
  }

  ParallelLaneUnderflowPolicy underflowPolicy() const noexcept
  {
    return underflowPolicy_;
  }

  // laneOutputs contains one mono buffer per enabled lane. When wetReady is
  // false, the configured underflow policy is applied. The output buffers must
  // be prepared for blockSize() frames.
  bool processBlock(const float* dryInput, const float* const* laneOutputs,
                    bool wetReady, float* outputLeft, float* outputRight,
                    std::size_t frames) noexcept;

  std::uint64_t underflowBlockCount() const noexcept
  {
    return underflowBlocks_.load(std::memory_order_relaxed);
  }

  void reset() noexcept;

private:
  struct Lane {
    float leftGain = 0.0f;
    float rightGain = 0.0f;
    bool enabled = true;
  };

  static Lane normalize(ParallelLaneMixConfig config) noexcept;

  std::vector<Lane> lanes_;
  std::vector<float> lastWetLeft_;
  std::vector<float> lastWetRight_;
  std::size_t blockSize_ = 0;
  float dryLeftGain_ = 0.70710678118f;
  float dryRightGain_ = 0.70710678118f;
  ParallelLaneUnderflowPolicy underflowPolicy_ =
    ParallelLaneUnderflowPolicy::DryFallback;
  std::atomic<std::uint64_t> underflowBlocks_{0};
};

} // namespace ardor
