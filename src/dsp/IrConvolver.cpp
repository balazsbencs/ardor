#include "IrConvolver.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

namespace ardor {

namespace {

constexpr float kPi = 3.14159265358979323846f;

size_t nextPowerOfTwo(size_t value)
{
  size_t out = 1;
  while (out < value) out <<= 1;
  return out;
}

void fft(std::vector<std::complex<float>>& a, bool inverse)
{
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(a[i], a[j]);
    }
  }

  for (size_t len = 2; len <= n; len <<= 1) {
    const float angle = 2.0f * kPi / static_cast<float>(len) * (inverse ? 1.0f : -1.0f);
    const std::complex<float> wlen(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (size_t j = 0; j < len / 2; ++j) {
        const auto u = a[i + j];
        const auto v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }

  if (inverse) {
    const float scale = 1.0f / static_cast<float>(n);
    for (auto& x : a) {
      x *= scale;
    }
  }
}

} // namespace

void IrConvolver::loadImpulse(std::vector<float> impulse)
{
  impulse_ = std::move(impulse);
  history_.assign(impulse_.size(), 0.0f);
  pos_ = 0;
  blockSize_ = 0;
  fftSize_ = 0;
  writeIndex_ = 0;
  overlap_.clear();
  scratch_.clear();
  sum_.clear();
  impulsePartitions_.clear();
  inputPartitions_.clear();
}

void IrConvolver::prepareBlockSize(size_t frames)
{
  if (frames == 0 || impulse_.empty() || blockSize_ == frames) {
    return;
  }
  preparePartitions(frames);
}

void IrConvolver::reset()
{
  std::fill(history_.begin(), history_.end(), 0.0f);
  pos_ = 0;
  writeIndex_ = 0;
  std::fill(overlap_.begin(), overlap_.end(), 0.0f);
  for (auto& partition : inputPartitions_) {
    std::fill(partition.begin(), partition.end(), std::complex<float>{});
  }
}

float IrConvolver::processSample(float input)
{
  if (impulse_.empty()) {
    return input;
  }

  history_[pos_] = input;

  float out = 0.0f;
  size_t h = pos_;
  for (float tap : impulse_) {
    out += tap * history_[h];
    h = (h == 0) ? history_.size() - 1 : h - 1;
  }

  pos_ = (pos_ + 1) % history_.size();
  return out;
}

void IrConvolver::preparePartitions(size_t frames)
{
  blockSize_ = frames;
  fftSize_ = nextPowerOfTwo(frames * 2);
  writeIndex_ = 0;

  const size_t partitionCount = (impulse_.size() + frames - 1) / frames;
  overlap_.assign(frames, 0.0f);
  scratch_.assign(fftSize_, {});
  sum_.assign(fftSize_, {});
  impulsePartitions_.assign(partitionCount, std::vector<std::complex<float>>(fftSize_));
  inputPartitions_.assign(partitionCount, std::vector<std::complex<float>>(fftSize_));

  for (size_t p = 0; p < partitionCount; ++p) {
    auto& partition = impulsePartitions_[p];
    std::fill(partition.begin(), partition.end(), std::complex<float>{});
    const size_t start = p * frames;
    const size_t count = std::min(frames, impulse_.size() - start);
    for (size_t i = 0; i < count; ++i) {
      partition[i] = impulse_[start + i];
    }
    fft(partition, false);
  }
}

void IrConvolver::processBlock(const float* input, float* output, size_t frames)
{
  if (frames == 0) return;
  if (impulse_.empty()) {
    std::copy(input, input + frames, output);
    return;
  }
  if (blockSize_ != frames) {
    if (blockSize_ == 0) {
      preparePartitions(frames);
    } else {
      for (size_t i = 0; i < frames; ++i) {
        output[i] = processSample(input[i]);
      }
      return;
    }
  }

  std::fill(scratch_.begin(), scratch_.end(), std::complex<float>{});
  for (size_t i = 0; i < frames; ++i) {
    scratch_[i] = input[i];
  }
  fft(scratch_, false);

  inputPartitions_[writeIndex_] = scratch_;

  std::fill(sum_.begin(), sum_.end(), std::complex<float>{});
  const size_t partitionCount = impulsePartitions_.size();
  for (size_t p = 0; p < partitionCount; ++p) {
    const size_t inputIndex = (writeIndex_ + partitionCount - p) % partitionCount;
    const auto& x = inputPartitions_[inputIndex];
    const auto& h = impulsePartitions_[p];
    for (size_t i = 0; i < fftSize_; ++i) {
      sum_[i] += x[i] * h[i];
    }
  }

  fft(sum_, true);

  for (size_t i = 0; i < frames; ++i) {
    output[i] = sum_[i].real() + overlap_[i];
    overlap_[i] = sum_[i + frames].real();
  }

  writeIndex_ = (writeIndex_ + 1) % partitionCount;
}

} // namespace ardor
