#pragma once

#include <cstddef>
#include <vector>

namespace ardor {

class IrConvolver {
public:
  void loadImpulse(std::vector<float> impulse);
  float processSample(float input);
  void reset();

private:
  std::vector<float> impulse_;
  std::vector<float> history_;
  size_t pos_ = 0;
};

} // namespace ardor
