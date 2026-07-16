#pragma once

#include "daisyfx/DaisyFxProcessor.h"
#include "dynamics/CompressorProcessor.h"
#include "equalizer/EqParameters.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
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
  bool addNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
              std::string id = "nam");
  void addCab(std::vector<float> impulse, float level, float mix, std::string id = "cab");
  void addDaisy(std::string id, DaisyFxProcessor processor);
  void addCompressor(std::string id, CompressorProcessor processor);
  bool addParametricEq(std::string id, const ParametricEqParams& params, float sampleRate, std::string& error);
  bool setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params);
  bool setDaisyParameter(const std::string& id, const std::string& key, float normalized);
  bool setCompressorParameter(const std::string& id, const std::string& key, float value);
  StereoSample process(StereoSample input, float cabLevel = 1.0f, float cabMix = 1.0f);
  // The live path processes complete, preallocated blocks. `input` is mono;
  // `left` and `right` receive the final stereo block.
  void processBlock(const float* input, float* left, float* right, size_t frames,
                    const float* cabLevels, const float* cabMixes);
  void reset();
  size_t tailFrames() const noexcept;
  uint64_t nonFiniteBlockCount() const noexcept;
  std::string firstNonFiniteBlockId() const;

private:
  struct Block;
  struct FaultState;
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
  std::shared_ptr<FaultState> faults_;
};

} // namespace ardor
