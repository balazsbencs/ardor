#pragma once

#include "dsp/SignalRouting.h"
#include "equalizer/EqParameters.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ardor {

class RuntimeChain;

struct DualRigLaneConfig {
  std::unique_ptr<RuntimeChain> chain;
  float level = 1.0f;
  bool polarityInverted = false;
};

class DualRigProcessor {
public:
  DualRigProcessor();
  ~DualRigProcessor();
  DualRigProcessor(const DualRigProcessor&) = delete;
  DualRigProcessor& operator=(const DualRigProcessor&) = delete;

  bool configure(DualRigLaneConfig left, DualRigLaneConfig right,
                 NamInputMode inputMode, double sampleRate, std::size_t blockSize,
                 bool requestParallel, int workerCpu, std::string& error);
  void prepareBlockSize(std::size_t frames);
  void processBlock(const float* inputLeft, const float* inputRight,
                    float* outputLeft, float* outputRight, std::size_t frames);
  void process(float inputLeft, float inputRight, float& outputLeft, float& outputRight);
  bool setDaisyParameter(const std::string& id, const std::string& key, float normalized);
  bool setCompressorParameter(const std::string& id, const std::string& key, float value);
  bool compressorGainReductionDb(const std::string& id, float& outDb) const;
  bool setNoiseGateParameter(const std::string& id, const std::string& key, float value);
  bool setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params);
  bool setParametricEqPassFilter(const std::string& id, EqPassFilterKind kind,
                                 const EqPassFilterParams& params);
  bool setBlockEnabled(const std::string& id, bool enabled);
  void reset();
  std::size_t tailFrames() const noexcept;
  bool parallelEnabled() const noexcept;
  uint64_t parallelWaitOverBudgetCount() const noexcept;

private:
  struct Lane {
    std::unique_ptr<RuntimeChain> chain;
    float level = 1.0f;
    float polarity = 1.0f;
    std::vector<float> firstOutput;
    std::vector<float> secondOutput;
  };

  void processLaneBlock(Lane& lane, bool takeRightOutput, const float* input,
                        float* output, std::size_t frames);
  float processLaneSample(Lane& lane, bool takeRightOutput, float input);

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
