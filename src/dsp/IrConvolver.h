#pragma once

#include <cstddef>
#include <complex>
#include <vector>

namespace ardor {

class IrConvolver {
public:
  void loadImpulse(std::vector<float> impulse);
  float processSample(float input);
  void processBlock(const float* input, float* output, size_t frames);
  void reset();

private:
  void preparePartitions(size_t frames);

  std::vector<float> impulse_;
  std::vector<float> history_;
  size_t pos_ = 0;

  size_t blockSize_ = 0;
  size_t fftSize_ = 0;
  size_t writeIndex_ = 0;
  std::vector<float> overlap_;
  std::vector<std::complex<float>> scratch_;
  std::vector<std::complex<float>> sum_;
  std::vector<std::vector<std::complex<float>>> impulsePartitions_;
  std::vector<std::vector<std::complex<float>>> inputPartitions_;
};

} // namespace ardor
