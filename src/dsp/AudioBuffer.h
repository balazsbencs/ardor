#pragma once

#include <cstddef>
#include <vector>

namespace ardor {

struct AudioBlock {
  std::vector<float> mono;
  std::vector<float> left;
  std::vector<float> right;

  explicit AudioBlock(size_t frames)
    : mono(frames, 0.0f), left(frames, 0.0f), right(frames, 0.0f)
  {
  }
};

} // namespace ardor
