#include "IrConvolver.h"

#include <algorithm>
#include <utility>

namespace ardor {

void IrConvolver::loadImpulse(std::vector<float> impulse)
{
  impulse_ = std::move(impulse);
  history_.assign(impulse_.size(), 0.0f);
  pos_ = 0;
}

void IrConvolver::reset()
{
  std::fill(history_.begin(), history_.end(), 0.0f);
  pos_ = 0;
}

float IrConvolver::processSample(float input)
{
  if (impulse_.empty()) {
    return input;
  }

  history_[pos_] = input;

  float out = 0.0f;
  size_t h = pos_;
  for (float tap : impulse_) {
    out += tap * history_[h];
    h = (h == 0) ? history_.size() - 1 : h - 1;
  }

  pos_ = (pos_ + 1) % history_.size();
  return out;
}

} // namespace ardor
