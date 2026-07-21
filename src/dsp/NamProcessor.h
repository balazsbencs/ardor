#pragma once

#include <filesystem>
#include <memory>
#include <cstddef>
#include <vector>

namespace nam {
class DSP;
}

namespace ardor {

class NamProcessor {
public:
  NamProcessor();
  ~NamProcessor();

  // `slimmableSize` selects a SlimmableContainer tier before it is prewarmed.
  // 0 is the nano tier and 1 is the full tier; non-slim models ignore it.
  bool load(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
            float slimmableSize = 1.0f);
  float process(float input);
  void processBlock(const float* input, float* output, size_t frames);
  void clear();
  void reset();
  bool loaded() const;

  // Returns tier breakpoints if the model is a SlimmableContainer; empty otherwise.
  std::vector<double> slimmableSizeBreakpoints() const;
  // No-op if model is not slimmable.
  void setSlimmableSize(double val);
  float slimmableSize() const noexcept { return slimmableSize_; }

private:
  std::unique_ptr<nam::DSP> model_;
  std::vector<float> input_{0.0f};
  std::vector<float> output_{0.0f};
  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  float normGain_ = 1.0f; // loudness normalization to kTargetLoudnessDb
  float slimmableSize_ = 1.0f;
};

} // namespace ardor
