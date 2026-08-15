#include "IrConvolver.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

namespace ardor {

namespace {

} // namespace

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
  fft_.prepare(fftSize_);

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
    fft_.transform(partition, false);
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
  fft_.transform(scratch_, false);

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

  fft_.transform(sum_, true);

  for (size_t i = 0; i < frames; ++i) {
    output[i] = sum_[i].real() + overlap_[i];
    overlap_[i] = sum_[i + frames].real();
  }

  writeIndex_ = (writeIndex_ + 1) % partitionCount;
}

} // namespace ardor
