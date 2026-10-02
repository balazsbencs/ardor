#include "dsp/ScheduledConvolver.h"

#include <algorithm>
#include <stdexcept>

namespace ardor {

void ScheduledConvolver::load(std::vector<float> impulse, std::size_t partitionFrames)
{
  impulse_ = std::move(impulse);
  partition_ = nextPowerOfTwo(std::max<std::size_t>(partitionFrames, 16));
  fftSize_ = partition_ * 2;
  frequencyBins_ = fftSize_ / 2 + 1;
  fft_.prepare(fftSize_);

  partitionCount_ = impulse_.empty()
      ? 0
      : (impulse_.size() + partition_ - 1) / partition_;

  impulseSpectra_ = prepareImpulse(impulse_);
  scratch_.assign(fftSize_, {});

  inputSpectra_.assign(std::max<std::size_t>(partitionCount_, 1),
                       std::vector<std::complex<float>>(frequencyBins_));
  accumulator_.assign(frequencyBins_, {});
  overlap_.assign(partition_, 0.0f);
  inBuffer_.assign(partition_, 0.0f);
  outBuffer_.assign(partition_, 0.0f);
  nextAccumulator_.assign(frequencyBins_, {});
  nextOverlap_.assign(partition_, 0.0f);
  nextOutBuffer_.assign(partition_, 0.0f);
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
  std::fill(nextAccumulator_.begin(), nextAccumulator_.end(), std::complex<float>{});
  std::fill(nextOverlap_.begin(), nextOverlap_.end(), 0.0f);
  std::fill(nextOutBuffer_.begin(), nextOutBuffer_.end(), 0.0f);
  transitioning_ = nullptr;
  fill_ = 0;
  newestInput_ = 0;
  beginPeriod();
}

ScheduledConvolver::PreparedImpulse ScheduledConvolver::prepareImpulse(
    const std::vector<float>& impulse) const
{
  if (impulse.size() != impulse_.size())
    throw std::invalid_argument("live convolution kernel must preserve impulse length");
  PreparedImpulse spectra(partitionCount_, std::vector<std::complex<float>>(frequencyBins_));
  std::vector<std::complex<float>> workspace(fftSize_);
  for (std::size_t p = 0; p < partitionCount_; ++p) {
    std::fill(workspace.begin(), workspace.end(), std::complex<float>{});
    const std::size_t start = p * partition_;
    const std::size_t count = std::min(partition_, impulse.size() - start);
    for (std::size_t i = 0; i < count; ++i) workspace[i] = impulse[start + i];
    fft_.transform(workspace, false);
    std::copy_n(workspace.begin(), frequencyBins_, spectra[p].begin());
  }
  return spectra;
}

void ScheduledConvolver::finishTransition() noexcept
{
  accumulator_.swap(nextAccumulator_);
  overlap_.swap(nextOverlap_);
  outBuffer_.swap(nextOutBuffer_);
  transitioning_ = nullptr;
}

void ScheduledConvolver::accumulate(std::size_t p,
    const std::vector<std::complex<float>>& x,
    const PreparedImpulse& current, const PreparedImpulse* next)
{
  const auto& h = current[p];
  for (std::size_t bin = 0; bin < frequencyBins_; ++bin)
    accumulator_[bin] += h[bin] * x[bin];
  if (next) {
    const auto& hn = (*next)[p];
    for (std::size_t bin = 0; bin < frequencyBins_; ++bin)
      nextAccumulator_[bin] += hn[bin] * x[bin];
  }
}

void ScheduledConvolver::beginPeriod()
{
  // Everything except the H[0] term is accumulated during the period. The
  // cursor walks p = 1 .. partitionCount_-1; schedulePending_ meters them out
  // so the last one lands just before the boundary.
  scheduleCursor_ = 1;
  schedulePending_ = 0;
  std::fill(accumulator_.begin(), accumulator_.end(), std::complex<float>{});
  if (transitioning_)
    std::fill(nextAccumulator_.begin(), nextAccumulator_.end(), std::complex<float>{});
}

void ScheduledConvolver::advanceSchedule(const PreparedImpulse& current,
                                        const PreparedImpulse* next)
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
    accumulate(p, inputSpectra_[slot], current, next);
  }
}

void ScheduledConvolver::closeBlock(const PreparedImpulse& current,
                                     const PreparedImpulse* next)
{
  // Store the block that just closed, then add the only term that needed it.
  std::fill(scratch_.begin(), scratch_.end(), std::complex<float>{});
  for (std::size_t i = 0; i < partition_; ++i) {
    scratch_[i] = inBuffer_[i];
  }
  fft_.transform(scratch_, false);

  newestInput_ = (newestInput_ + 1) % inputSpectra_.size();
  std::copy(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(frequencyBins_),
            inputSpectra_[newestInput_].begin());

  if (partitionCount_ > 0) accumulate(0, scratch_, current, next);

  // Any passes the schedule did not reach — possible when the impulse has more
  // partitions than the period has samples — are settled here so the result is
  // always complete.
  while (scheduleCursor_ < partitionCount_) {
    const std::size_t p = scheduleCursor_++;
    const std::size_t slot =
        (newestInput_ + inputSpectra_.size() - p) % inputSpectra_.size();
    accumulate(p, inputSpectra_[slot], current, next);
  }

  render(accumulator_, overlap_, outBuffer_);
  if (next) render(nextAccumulator_, nextOverlap_, nextOutBuffer_);
  beginPeriod();
}

void ScheduledConvolver::render(std::vector<std::complex<float>>& accumulator,
                               std::vector<float>& overlap, std::vector<float>& output)
{
  std::copy(accumulator.begin(), accumulator.end(), scratch_.begin());
  scratch_[0] = {scratch_[0].real(), 0.0f};
  scratch_[fftSize_ / 2] = {scratch_[fftSize_ / 2].real(), 0.0f};
  for (std::size_t bin = 1; bin < fftSize_ / 2; ++bin)
    scratch_[fftSize_ - bin] = std::conj(scratch_[bin]);
  fft_.transform(scratch_, true);
  for (std::size_t i = 0; i < partition_; ++i) {
    output[i] = scratch_[i].real() + overlap[i];
    overlap[i] = scratch_[i + partition_].real();
  }
}

float ScheduledConvolver::process(float input)
{
  return processFrame(input, nullptr, nullptr).current;
}

ScheduledConvolver::Frame ScheduledConvolver::processFrame(
    float input, const PreparedImpulse* current, const PreparedImpulse* next)
{
  if (impulse_.empty() || partition_ == 0) return {input, input};
  if (next != transitioning_) {
    std::fill(nextAccumulator_.begin(), nextAccumulator_.end(), std::complex<float>{});
    std::fill(nextOverlap_.begin(), nextOverlap_.end(), 0.0f);
    std::fill(nextOutBuffer_.begin(), nextOutBuffer_.end(), 0.0f);
    transitioning_ = next;
  }
  const Frame out{outBuffer_[fill_], next ? nextOutBuffer_[fill_] : outBuffer_[fill_]};
  inBuffer_[fill_] = input;
  ++fill_;
  const auto& kernel = current ? *current : impulseSpectra_;
  advanceSchedule(kernel, next);
  if (fill_ == partition_) {
    closeBlock(kernel, next);
    fill_ = 0;
  }
  return out;
}

} // namespace ardor
