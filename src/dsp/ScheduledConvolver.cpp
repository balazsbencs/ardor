#include "dsp/ScheduledConvolver.h"

#include <algorithm>

namespace ardor {

void ScheduledConvolver::load(std::vector<float> impulse, std::size_t partitionFrames)
{
  impulse_ = std::move(impulse);
  partition_ = nextPowerOfTwo(std::max<std::size_t>(partitionFrames, 16));
  fftSize_ = partition_ * 2;
  fft_.prepare(fftSize_);

  partitionCount_ = impulse_.empty()
      ? 0
      : (impulse_.size() + partition_ - 1) / partition_;

  impulseSpectra_.assign(partitionCount_, std::vector<std::complex<float>>(fftSize_));
  for (std::size_t p = 0; p < partitionCount_; ++p) {
    auto& spectrum = impulseSpectra_[p];
    std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
    const std::size_t start = p * partition_;
    const std::size_t count = std::min(partition_, impulse_.size() - start);
    for (std::size_t i = 0; i < count; ++i) {
      spectrum[i] = impulse_[start + i];
    }
    fft_.transform(spectrum, false);
  }

  inputSpectra_.assign(std::max<std::size_t>(partitionCount_, 1),
                       std::vector<std::complex<float>>(fftSize_));
  accumulator_.assign(fftSize_, {});
  scratch_.assign(fftSize_, {});
  overlap_.assign(partition_, 0.0f);
  inBuffer_.assign(partition_, 0.0f);
  outBuffer_.assign(partition_, 0.0f);
  reset();
}

void ScheduledConvolver::reset()
{
  for (auto& spectrum : inputSpectra_) {
    std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{});
  }
  std::fill(accumulator_.begin(), accumulator_.end(), std::complex<float>{});
  std::fill(overlap_.begin(), overlap_.end(), 0.0f);
  std::fill(inBuffer_.begin(), inBuffer_.end(), 0.0f);
  std::fill(outBuffer_.begin(), outBuffer_.end(), 0.0f);
  fill_ = 0;
  newestInput_ = 0;
  beginPeriod();
}

void ScheduledConvolver::beginPeriod()
{
  // Everything except the H[0] term is accumulated during the period. The
  // cursor walks p = 1 .. partitionCount_-1; schedulePending_ meters them out
  // so the last one lands just before the boundary.
  scheduleCursor_ = 1;
  schedulePending_ = 0;
  std::fill(accumulator_.begin(), accumulator_.end(), std::complex<float>{});
}

void ScheduledConvolver::advanceSchedule()
{
  if (partitionCount_ <= 1) return;

  // Integer rate control: add (count-1) work units per sample and spend one
  // multiply pass for every `partition_` units accrued. Over a full period that
  // is exactly count-1 passes, evenly spaced, with no division per sample.
  schedulePending_ += partitionCount_ - 1;
  while (schedulePending_ >= partition_ && scheduleCursor_ < partitionCount_) {
    schedulePending_ -= partition_;

    const std::size_t p = scheduleCursor_++;
    // X[n-p]: newestInput_ holds X[n-1] at this point in the period, because
    // the block that closes this period has not been stored yet.
    const std::size_t slot =
        (newestInput_ + inputSpectra_.size() - (p - 1)) % inputSpectra_.size();
    const auto& h = impulseSpectra_[p];
    const auto& x = inputSpectra_[slot];
    for (std::size_t bin = 0; bin < fftSize_; ++bin) {
      accumulator_[bin] += h[bin] * x[bin];
    }
  }
}

void ScheduledConvolver::closeBlock()
{
  // Store the block that just closed, then add the only term that needed it.
  std::fill(scratch_.begin(), scratch_.end(), std::complex<float>{});
  for (std::size_t i = 0; i < partition_; ++i) {
    scratch_[i] = inBuffer_[i];
  }
  fft_.transform(scratch_, false);

  newestInput_ = (newestInput_ + 1) % inputSpectra_.size();
  std::copy(scratch_.begin(), scratch_.end(), inputSpectra_[newestInput_].begin());

  if (partitionCount_ > 0) {
    const auto& h = impulseSpectra_[0];
    for (std::size_t bin = 0; bin < fftSize_; ++bin) {
      accumulator_[bin] += h[bin] * scratch_[bin];
    }
  }

  // Any passes the schedule did not reach — possible when the impulse has more
  // partitions than the period has samples — are settled here so the result is
  // always complete.
  while (scheduleCursor_ < partitionCount_) {
    const std::size_t p = scheduleCursor_++;
    const std::size_t slot =
        (newestInput_ + inputSpectra_.size() - p) % inputSpectra_.size();
    const auto& h = impulseSpectra_[p];
    const auto& x = inputSpectra_[slot];
    for (std::size_t bin = 0; bin < fftSize_; ++bin) {
      accumulator_[bin] += h[bin] * x[bin];
    }
  }

  fft_.transform(accumulator_, true);
  for (std::size_t i = 0; i < partition_; ++i) {
    outBuffer_[i] = accumulator_[i].real() + overlap_[i];
    overlap_[i] = accumulator_[i + partition_].real();
  }

  beginPeriod();
}

float ScheduledConvolver::process(float input)
{
  if (impulse_.empty() || partition_ == 0) return input;

  const float out = outBuffer_[fill_];
  inBuffer_[fill_] = input;
  ++fill_;

  advanceSchedule();

  if (fill_ == partition_) {
    closeBlock();
    fill_ = 0;
  }
  return out;
}

} // namespace ardor
