#pragma once

#include "dsp/IrConvolver.h"
#include "dsp/NamProcessor.h"
#include "dsp/SignalRouting.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

struct DualAmpLaneConfig {
  std::filesystem::path modelPath;
  std::vector<float> impulse;
  float slimmableSize = 1.0f;
  float cabLevel = 1.0f;
  float cabMix = 1.0f;
  bool polarityInverted = false;
};

class DualAmpProcessor {
public:
  DualAmpProcessor();
  ~DualAmpProcessor();
  DualAmpProcessor(const DualAmpProcessor&) = delete;
  DualAmpProcessor& operator=(const DualAmpProcessor&) = delete;

  bool configure(DualAmpLaneConfig left, DualAmpLaneConfig right,
                 NamInputMode inputMode, double sampleRate, int maxBlockSize,
                 bool requestParallel, int workerCpu, std::string& error);
  void prepareBlockSize(std::size_t frames);
  void processBlock(const float* inputLeft, const float* inputRight,
                    float* outputLeft, float* outputRight, std::size_t frames);
  void process(float inputLeft, float inputRight, float& outputLeft, float& outputRight);
  void reset();
  std::size_t tailFrames() const noexcept;
  bool parallelEnabled() const noexcept;
  uint64_t parallelWaitOverBudgetCount() const noexcept;

private:
  struct Lane {
    NamProcessor nam;
    IrConvolver cab;
    float cabLevel = 1.0f;
    float cabMix = 1.0f;
    float polarity = 1.0f;
    std::vector<float> namOutput;
    std::vector<float> cabOutput;
  };

  void processLaneBlock(Lane& lane, const float* input, float* output, std::size_t frames);
  float processLaneSample(Lane& lane, float input);

  Lane left_;
  Lane right_;
  NamInputMode inputMode_ = NamInputMode::Sum;
  std::size_t blockSize_ = 0;
  double sampleRate_ = 48000.0;
  std::vector<float> monoInput_;

  struct ParallelState;
  ParallelState* parallel_ = nullptr;
};

} // namespace ardor
