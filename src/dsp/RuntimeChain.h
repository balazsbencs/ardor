#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "dynamics/CompressorProcessor.h"

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
  RuntimeChain(const RuntimeChain&) = delete;
  RuntimeChain& operator=(const RuntimeChain&) = delete;
  RuntimeChain(RuntimeChain&&) noexcept;
  RuntimeChain& operator=(RuntimeChain&&) noexcept;

  void prepareBlockSize(size_t frames);
  void clear();
  bool addNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  void addCab(std::vector<float> impulse, float level, float mix);
  void addDaisy(DaisyFxProcessor processor);
  void addCompressor(CompressorProcessor processor);
  StereoSample process(StereoSample input, float cabLevel = 1.0f, float cabMix = 1.0f);
  // The live path processes complete, preallocated blocks. `input` is mono;
  // `left` and `right` receive the final stereo block.
  void processBlock(const float* input, float* left, float* right, size_t frames,
                    const float* cabLevels, const float* cabMixes);
  void reset();
  size_t tailFrames() const noexcept;

private:
  struct Block;
  std::vector<Block> blocks_;
  size_t blockSize_ = 0;

  // Ping-pong storage owned by the chain. It is sized by prepareBlockSize(),
  // never resized by the normal realtime path, and lets adjacent processors
  // exchange whole blocks without hidden in-place assumptions.
  std::vector<float> leftA_;
  std::vector<float> rightA_;
  std::vector<float> leftB_;
  std::vector<float> rightB_;
  std::vector<float> monoScratch_;
};

} // namespace ardor
