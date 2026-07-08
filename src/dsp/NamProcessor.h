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

  bool load(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize);
  float process(float input);
  void processBlock(const float* input, float* output, size_t frames);
  void clear();
  void reset();
  bool loaded() const;

private:
  std::unique_ptr<nam::DSP> model_;
  std::vector<float> input_{0.0f};
  std::vector<float> output_{0.0f};
  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
};

} // namespace ardor
