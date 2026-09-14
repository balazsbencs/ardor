#include "dsp/ParallelLaneMixer.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr float kQuarterPi = 0.78539816339744830962f;
constexpr float kSqrtHalf = 0.70710678118654752440f;

} // namespace

ParallelLaneMixer::Lane ParallelLaneMixer::normalize(
  ParallelLaneMixConfig config) noexcept
{
  const float level = std::isfinite(config.level) ? std::max(config.level, 0.0f) : 0.0f;
  const float pan = std::isfinite(config.pan) ? std::clamp(config.pan, -1.0f, 1.0f) : 0.0f;
  const float angle = (pan + 1.0f) * kQuarterPi;
  return {
    level * std::cos(angle),
    level * std::sin(angle),
    config.enabled,
  };
}

bool ParallelLaneMixer::prepare(std::size_t laneCount, std::size_t blockSize)
{
  if (laneCount == 0 || blockSize == 0) return false;
  lanes_.assign(laneCount, Lane{});
  lastWetLeft_.assign(blockSize, 0.0f);
  lastWetRight_.assign(blockSize, 0.0f);
  blockSize_ = blockSize;
  dryLeftGain_ = kSqrtHalf;
  dryRightGain_ = kSqrtHalf;
  underflowPolicy_ = ParallelLaneUnderflowPolicy::DryFallback;
  underflowBlocks_.store(0, std::memory_order_relaxed);
  return true;
}

bool ParallelLaneMixer::setLane(std::size_t lane,
                                ParallelLaneMixConfig config) noexcept
{
  if (lane >= lanes_.size()) return false;
  lanes_[lane] = normalize(config);
  return true;
}

void ParallelLaneMixer::setDry(ParallelLaneMixConfig config) noexcept
{
  const Lane normalized = normalize(config);
  dryLeftGain_ = normalized.enabled ? normalized.leftGain : 0.0f;
  dryRightGain_ = normalized.enabled ? normalized.rightGain : 0.0f;
}

bool ParallelLaneMixer::processBlock(const float* dryInput,
                                     const float* const* laneOutputs,
                                     bool wetReady,
                                     float* outputLeft,
                                     float* outputRight,
                                     std::size_t frames) noexcept
{
  if (blockSize_ == 0 || frames != blockSize_ || !outputLeft || !outputRight) return false;

  bool usableWet = wetReady && laneOutputs != nullptr;
  if (usableWet) {
    for (std::size_t lane = 0; lane < lanes_.size(); ++lane) {
      if (lanes_[lane].enabled && !laneOutputs[lane]) {
        usableWet = false;
        break;
      }
    }
  }

  if (usableWet) {
    std::fill(lastWetLeft_.begin(), lastWetLeft_.end(), 0.0f);
    std::fill(lastWetRight_.begin(), lastWetRight_.end(), 0.0f);
    for (std::size_t lane = 0; lane < lanes_.size(); ++lane) {
      const Lane& config = lanes_[lane];
      if (!config.enabled) continue;
      const float* input = laneOutputs[lane];
      for (std::size_t i = 0; i < frames; ++i) {
        lastWetLeft_[i] += input[i] * config.leftGain;
        lastWetRight_[i] += input[i] * config.rightGain;
      }
    }
  } else {
    underflowBlocks_.fetch_add(1, std::memory_order_relaxed);
  }

  for (std::size_t i = 0; i < frames; ++i) {
    if (!usableWet && underflowPolicy_ == ParallelLaneUnderflowPolicy::Silence) {
      outputLeft[i] = 0.0f;
      outputRight[i] = 0.0f;
      continue;
    }

    const float dry = dryInput ? dryInput[i] : 0.0f;
    const bool useHeldWet = !usableWet
      && underflowPolicy_ == ParallelLaneUnderflowPolicy::HoldLastWet;
    const float wetLeft = useHeldWet || usableWet ? lastWetLeft_[i] : 0.0f;
    const float wetRight = useHeldWet || usableWet ? lastWetRight_[i] : 0.0f;
    outputLeft[i] = dry * dryLeftGain_ + wetLeft;
    outputRight[i] = dry * dryRightGain_ + wetRight;
  }
  return true;
}

void ParallelLaneMixer::reset() noexcept
{
  std::fill(lastWetLeft_.begin(), lastWetLeft_.end(), 0.0f);
  std::fill(lastWetRight_.begin(), lastWetRight_.end(), 0.0f);
  underflowBlocks_.store(0, std::memory_order_relaxed);
}

} // namespace ardor
