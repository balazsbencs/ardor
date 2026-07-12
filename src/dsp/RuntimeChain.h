#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace ardor {

class IrConvolver;
class NamProcessor;

class RuntimeChain {
public:
  RuntimeChain();
  ~RuntimeChain();

  void prepareBlockSize(size_t frames);
  void clear();
  bool addNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  void addCab(std::vector<float> impulse, float level, float mix);
  void addDaisy(DaisyFxProcessor processor);
  void setCabParams(float level, float mix);
  StereoSample process(StereoSample input);
  void reset();

private:
  struct Block;
  std::vector<Block> blocks_;
  size_t blockSize_ = 0;
};

} // namespace ardor
