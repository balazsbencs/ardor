#pragma once

#include <optional>

namespace ardor {

struct ExpressionFilterConfig {
  int minimumRaw = 0;
  int maximumRaw = 26400; // 3.3 V with the ADS1115 ±4.096 V range.
  float smoothing = 0.25f;
  float deadband = 0.002f;
};

// Converts ADC codes into a stable 0..1 control value. This class owns no
// device I/O and is safe to test on desktop builds.
class ExpressionFilter {
public:
  explicit ExpressionFilter(ExpressionFilterConfig config = {});

  bool valid() const noexcept;
  std::optional<float> update(int raw);
  void reset();

private:
  ExpressionFilterConfig config_;
  bool initialized_ = false;
  float smoothed_ = 0.0f;
  std::optional<float> lastEmitted_;
};

} // namespace ardor
