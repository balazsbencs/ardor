#include "control/Expression.h"

#include <algorithm>
#include <cmath>

namespace ardor {

ExpressionFilter::ExpressionFilter(ExpressionFilterConfig config)
  : config_(config)
{
}

bool ExpressionFilter::valid() const noexcept
{
  return config_.maximumRaw > config_.minimumRaw
    && std::isfinite(config_.smoothing)
    && config_.smoothing > 0.0f
    && config_.smoothing <= 1.0f
    && std::isfinite(config_.deadband)
    && config_.deadband >= 0.0f
    && config_.deadband <= 1.0f;
}

std::optional<float> ExpressionFilter::update(int raw)
{
  if (!valid()) return std::nullopt;

  const float normalized = std::clamp(
    static_cast<float>(raw - config_.minimumRaw)
      / static_cast<float>(config_.maximumRaw - config_.minimumRaw),
    0.0f, 1.0f);
  if (!initialized_) {
    smoothed_ = normalized;
    initialized_ = true;
  } else {
    smoothed_ += config_.smoothing * (normalized - smoothed_);
  }

  if (lastEmitted_
      && std::fabs(smoothed_ - *lastEmitted_) < config_.deadband) {
    return std::nullopt;
  }
  lastEmitted_ = smoothed_;
  return smoothed_;
}

void ExpressionFilter::reset()
{
  initialized_ = false;
  smoothed_ = 0.0f;
  lastEmitted_.reset();
}

} // namespace ardor
