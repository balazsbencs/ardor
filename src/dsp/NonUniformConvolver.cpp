#include "dsp/NonUniformConvolver.h"

#include <algorithm>

namespace ardor {

void NonUniformConvolver::load(std::vector<float> impulse)
{
  impulseFrames_ = impulse.size();
  const std::size_t split = std::min(impulse.size(), EARLY_IMPULSE_FRAMES);
  std::vector<float> earlyImpulse(impulse.begin(), impulse.begin() + static_cast<std::ptrdiff_t>(split));
  early_.load(std::move(earlyImpulse), EARLY_PARTITION_FRAMES);

  tailLoaded_ = impulse.size() > split;
  if (tailLoaded_) {
    std::vector<float> tailImpulse(impulse.begin() + static_cast<std::ptrdiff_t>(split), impulse.end());
    tail_.load(std::move(tailImpulse), TAIL_PARTITION_FRAMES);
  } else {
    tail_.load({}, TAIL_PARTITION_FRAMES);
  }
  tailDelay_.assign(TAIL_ALIGNMENT_FRAMES, 0.0f);
  nextTailDelay_.assign(TAIL_ALIGNMENT_FRAMES, 0.0f);
  tailDelayPosition_ = 0;
}

void NonUniformConvolver::reset()
{
  early_.reset();
  tail_.reset();
  std::fill(tailDelay_.begin(), tailDelay_.end(), 0.0f);
  std::fill(nextTailDelay_.begin(), nextTailDelay_.end(), 0.0f);
  tailDelayPosition_ = 0;
}

float NonUniformConvolver::process(float input)
{
  return processFrame(input, nullptr, nullptr).current;
}

NonUniformConvolver::PreparedImpulse NonUniformConvolver::prepareImpulse(
    const std::vector<float>& impulse) const
{
  const std::size_t split = std::min(impulse.size(), EARLY_IMPULSE_FRAMES);
  return {
    early_.prepareImpulse({impulse.begin(), impulse.begin() + static_cast<std::ptrdiff_t>(split)}),
    tail_.prepareImpulse({impulse.begin() + static_cast<std::ptrdiff_t>(split), impulse.end()}),
  };
}

void NonUniformConvolver::finishTransition() noexcept
{
  early_.finishTransition();
  if (tailLoaded_) tail_.finishTransition();
  tailDelay_.swap(nextTailDelay_);
}

NonUniformConvolver::Frame NonUniformConvolver::processFrame(
    float input, const PreparedImpulse* current, const PreparedImpulse* next)
{
  if (!loaded()) return {input, input};
  const auto early = early_.processFrame(input, current ? &current->early : nullptr,
                                        next ? &next->early : nullptr);
  if (!tailLoaded_) return early;

  const auto tail = tail_.processFrame(input, current ? &current->tail : nullptr,
                                      next ? &next->tail : nullptr);
  if constexpr (TAIL_ALIGNMENT_FRAMES == 0) {
    return {early.current + tail.current, early.next + tail.next};
  } else {
    const float alignedTail = tailDelay_[tailDelayPosition_];
    const float nextAlignedTail = nextTailDelay_[tailDelayPosition_];
    tailDelay_[tailDelayPosition_] = tail.current;
    nextTailDelay_[tailDelayPosition_] = tail.next;
    tailDelayPosition_ = (tailDelayPosition_ + 1) % tailDelay_.size();
    return {early.current + alignedTail, early.next + nextAlignedTail};
  }
}

} // namespace ardor
