#pragma once

#include "IrConvolver.h"

#include <utility>
#include <vector>

namespace ardor {

class PedalEngine {
public:
  void loadIr(std::vector<float> impulse);
  void setInputGain(float gain);
  void setOutputGain(float gain);
  std::pair<float, float> process(float input);
  void reset();

private:
  float inputGain_ = 1.0f;
  float outputGain_ = 1.0f;
  IrConvolver ir_;
};

} // namespace ardor
