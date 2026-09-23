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
  tailDelayPosition_ = 0;
}

void NonUniformConvolver::reset()
{
  early_.reset();
  tail_.reset();
  std::fill(tailDelay_.begin(), tailDelay_.end(), 0.0f);
  tailDelayPosition_ = 0;
}

float NonUniformConvolver::process(float input)
{
  if (!loaded()) return input;
  const float early = early_.process(input);
  if (!tailLoaded_) return early;

  const float tail = tail_.process(input);
  if constexpr (TAIL_ALIGNMENT_FRAMES == 0) {
    return early + tail;
  } else {
    const float alignedTail = tailDelay_[tailDelayPosition_];
    tailDelay_[tailDelayPosition_] = tail;
    tailDelayPosition_ = (tailDelayPosition_ + 1) % tailDelay_.size();
    return early + alignedTail;
  }
}

} // namespace ardor
