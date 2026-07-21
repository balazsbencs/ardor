#include "IrConvolver.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

namespace ardor {

namespace {

constexpr double kPi = 3.14159265358979323846;

size_t nextPowerOfTwo(size_t value)
{
  size_t out = 1;
  while (out < value) out <<= 1;
  return out;
}

} // namespace

void IrConvolver::prepareFftTables()
{
  bitReverse_.assign(fftSize_, 0);
  for (size_t i = 1, j = 0; i < fftSize_; ++i) {
    size_t bit = fftSize_ >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    bitReverse_[i] = j;
  }

  // One table for the largest size; stage `len` reads it at stride fftSize_/len.
  // Computed in double so table entries carry no accumulated rounding.
  twiddles_.assign(fftSize_ / 2, {});
  for (size_t j = 0; j < fftSize_ / 2; ++j) {
    const double angle = -2.0 * kPi * static_cast<double>(j) / static_cast<double>(fftSize_);
    twiddles_[j] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
  }
}

void IrConvolver::fftInPlace(std::vector<std::complex<float>>& values, bool inverse) const
{
  const size_t n = values.size();
  for (size_t i = 1; i < n; ++i) {
    const size_t j = bitReverse_[i];
    if (i < j) {
      std::swap(values[i], values[j]);
    }
  }

  for (size_t len = 2; len <= n; len <<= 1) {
    const size_t stride = n / len;
    for (size_t i = 0; i < n; i += len) {
      for (size_t j = 0; j < len / 2; ++j) {
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

void IrConvolver::loadImpulse(std::vector<float> impulse)
{
  impulse_ = std::move(impulse);
  history_.assign(impulse_.size(), 0.0f);
  pos_ = 0;
  blockSize_ = 0;
  blockSizeMismatchCount_ = 0;
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

size_t IrConvolver::tailFrames() const noexcept
{
  return impulse_.empty() ? 0 : impulse_.size() - 1;
}

uint64_t IrConvolver::blockSizeMismatchCount() const noexcept
{
  return blockSizeMismatchCount_;
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
  prepareFftTables();

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
    fftInPlace(partition, false);
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
      // Partitioned overlap-add state cannot be combined correctly with the
      // direct-convolution history. Never turn a malformed realtime quantum
      // into an unbounded O(IR length) fallback; the adapter must supply the
      // prepared size, so contain this contract violation as silence instead.
      ++blockSizeMismatchCount_;
      std::fill(output, output + frames, 0.0f);
      return;
    }
  }

  std::fill(scratch_.begin(), scratch_.end(), std::complex<float>{});
  for (size_t i = 0; i < frames; ++i) {
    scratch_[i] = input[i];
  }
  fftInPlace(scratch_, false);

  // Both vectors are allocated during preparePartitions(). Copy into the
  // existing slot so the realtime path never relies on vector assignment
  // capacity behavior.
  std::copy(scratch_.begin(), scratch_.end(), inputPartitions_[writeIndex_].begin());

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

  fftInPlace(sum_, true);

  for (size_t i = 0; i < frames; ++i) {
    output[i] = sum_[i].real() + overlap_[i];
    overlap_[i] = sum_[i + frames].real();
  }

  writeIndex_ = (writeIndex_ + 1) % partitionCount;
}

} // namespace ardor
