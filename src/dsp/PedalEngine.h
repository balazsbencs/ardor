#pragma once

#include "IrConvolver.h"
#include "NamProcessor.h"

#include <filesystem>
#include <utility>
#include <vector>

namespace ardor {

class PedalEngine {
public:
  bool loadNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  void loadIr(std::vector<float> impulse);
  void setInputGain(float gain);
  void setOutputGain(float gain);
  std::pair<float, float> process(float input);
  void reset();

private:
  float inputGain_ = 1.0f;
  float outputGain_ = 1.0f;
  NamProcessor nam_;
  IrConvolver ir_;
};

} // namespace ardor
