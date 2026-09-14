#pragma once

#include "dsp/ScheduledConvolver.h"

#include <cstddef>
#include <vector>

namespace ardor {

// Two-stage convolution for long room impulses. The first 1024 taps use small
// partitions so reflections start promptly; the remainder keeps large,
// scheduled partitions so long tails do not multiply the steady-state cost.
class NonUniformConvolver {
public:
  static constexpr std::size_t EARLY_PARTITION_FRAMES = 128;
  static constexpr std::size_t EARLY_IMPULSE_FRAMES = 1024;
  static constexpr std::size_t TAIL_PARTITION_FRAMES = 1024;

  void load(std::vector<float> impulse);
  void reset();
  float process(float input);

  bool loaded() const noexcept { return impulseFrames_ != 0; }
  std::size_t latencyFrames() const noexcept { return EARLY_PARTITION_FRAMES; }
  std::size_t impulseFrames() const noexcept { return impulseFrames_; }

private:
  static constexpr std::size_t TAIL_ALIGNMENT_FRAMES =
      EARLY_IMPULSE_FRAMES + EARLY_PARTITION_FRAMES - TAIL_PARTITION_FRAMES;

  ScheduledConvolver early_;
  ScheduledConvolver tail_;
  std::vector<float> tailDelay_;
  std::size_t tailDelayPosition_ = 0;
  std::size_t impulseFrames_ = 0;
  bool tailLoaded_ = false;
};

} // namespace ardor
