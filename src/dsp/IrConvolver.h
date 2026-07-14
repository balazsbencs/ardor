#pragma once

#include <cstddef>
#include <complex>
#include <vector>

namespace ardor {

class IrConvolver {
public:
  void loadImpulse(std::vector<float> impulse);
  void prepareBlockSize(size_t frames);
  float processSample(float input);
  void processBlock(const float* input, float* output, size_t frames);
  void reset();
  size_t tailFrames() const noexcept;

private:
  void preparePartitions(size_t frames);
  void prepareFftTables();
  void fftInPlace(std::vector<std::complex<float>>& values, bool inverse) const;

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
  std::vector<size_t> bitReverse_;
  std::vector<std::complex<float>> twiddles_;
};

} // namespace ardor
