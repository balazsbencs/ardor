#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ardor {

// Radix-2 in-place FFT with precomputed bit-reversal and twiddle tables.
//
// Extracted from IrConvolver so the scheduled convolver can share it rather
// than carry a second copy. Tables are built once in prepare(); transform()
// allocates nothing and is safe to call from the audio thread.
class RealtimeFft {
public:
  // `size` must be a power of two.
  void prepare(std::size_t size);
  std::size_t size() const noexcept { return size_; }

  void transform(std::vector<std::complex<float>>& values, bool inverse) const;

private:
  std::size_t size_ = 0;
  std::vector<std::size_t> bitReverse_;
  std::vector<std::complex<float>> twiddles_;
};

// Smallest power of two greater than or equal to `value`.
std::size_t nextPowerOfTwo(std::size_t value);

} // namespace ardor
