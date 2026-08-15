#include "dsp/RealtimeFft.h"

#include <cmath>
#include <utility>

namespace ardor {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

std::size_t nextPowerOfTwo(std::size_t value)
{
  std::size_t out = 1;
  while (out < value) out <<= 1;
  return out;
}

void RealtimeFft::prepare(std::size_t size)
{
  size_ = size;
  bitReverse_.assign(size_, 0);
  for (std::size_t i = 1, j = 0; i < size_; ++i) {
    std::size_t bit = size_ >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    bitReverse_[i] = j;
  }

  // One table for the largest size; stage `len` reads it at stride size_/len.
  // Computed in double so table entries carry no accumulated rounding.
  twiddles_.assign(size_ / 2, {});
  for (std::size_t j = 0; j < size_ / 2; ++j) {
    const double angle = -2.0 * kPi * static_cast<double>(j) / static_cast<double>(size_);
    twiddles_[j] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
  }
}

void RealtimeFft::transform(std::vector<std::complex<float>>& values, bool inverse) const
{
  const std::size_t n = values.size();
  for (std::size_t i = 1; i < n; ++i) {
    const std::size_t j = bitReverse_[i];
    if (i < j) {
      std::swap(values[i], values[j]);
    }
  }

  for (std::size_t len = 2; len <= n; len <<= 1) {
    const std::size_t stride = n / len;
    for (std::size_t i = 0; i < n; i += len) {
      for (std::size_t j = 0; j < len / 2; ++j) {
        std::complex<float> w = twiddles_[j * stride];
        if (inverse) {
          w = std::conj(w);
        }
        const auto u = values[i + j];
        const auto v = values[i + j + len / 2] * w;
        values[i + j] = u + v;
        values[i + j + len / 2] = u - v;
      }
    }
  }

  if (inverse) {
    const float scale = 1.0f / static_cast<float>(n);
    for (auto& x : values) {
      x *= scale;
    }
  }
}

} // namespace ardor
