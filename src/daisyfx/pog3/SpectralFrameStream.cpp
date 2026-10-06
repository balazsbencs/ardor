#include "daisyfx/pog3/SpectralFrameStream.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ardor::pog3 {

SpectralPlan::SpectralPlan(std::size_t frameSize, std::size_t hopSize) : hopSize_(hopSize) {
  if (frameSize < 32 || frameSize > 32768 || (frameSize & (frameSize - 1)) != 0
      || hopSize == 0 || hopSize > frameSize / 2 || frameSize % hopSize != 0)
    throw std::invalid_argument("POG3 spectral plan requires a power-of-two frame and a dividing hop <= N/2");
  fft_.prepare(frameSize);
  window_.resize(frameSize);
  synthesis_.resize(frameSize);
  constexpr double kTwoPi = 6.2831853071795864769;
  for (std::size_t i = 0; i < frameSize; ++i)
    window_[i] = static_cast<float>(.5 - .5 * std::cos(kTwoPi * static_cast<double>(i) / frameSize));
  // Derive WOLA gain, including phase-dependent normalization at N/2 hop.
  // No assumption about Hann, sqrt-Hann, or a particular overlap factor.
  for (std::size_t phase = 0; phase < hopSize; ++phase) {
    double gain = 0;
    for (std::size_t i = phase; i < frameSize; i += hopSize)
      gain += static_cast<double>(window_[i]) * window_[i];
    for (std::size_t i = phase; i < frameSize; i += hopSize)
      synthesis_[i] = static_cast<float>(window_[i] / gain);
  }
}

void SpectralPlan::transform(std::vector<std::complex<float>>& values, bool inverse) const {
  assert(values.size() == frameSize());
  fft_.transform(values, inverse);
}

void SpectralAnalysis::prepare(std::shared_ptr<const SpectralPlan> plan) {
  if (!plan) throw std::invalid_argument("POG3 analysis requires a spectral plan");
  plan_ = std::move(plan);
  history_.resize(plan_->frameSize());
  spectrum_.resize(plan_->frameSize());
  reset();
}

void SpectralAnalysis::reset() noexcept {
  std::fill(history_.begin(), history_.end(), 0.0f);
  std::fill(spectrum_.begin(), spectrum_.end(), std::complex<float>{});
  write_ = frameCount_ = 0;
  untilFrame_ = plan_ ? plan_->hopSize() : 0;
}

bool SpectralAnalysis::push(float sample) noexcept {
  if (!plan_) return false;
  // A nonfinite input is never allowed to poison future windows.
  history_[write_] = std::isfinite(sample) ? sample : 0.0f;
  write_ = (write_ + 1) & (history_.size() - 1);
  if (--untilFrame_ != 0) return false;
  const auto window = plan_->analysisWindow();
  for (std::size_t i = 0; i < spectrum_.size(); ++i)
    spectrum_[i] = {history_[(write_ + i) & (history_.size() - 1)] * window[i], 0};
  plan_->transform(spectrum_, false);
  ++frameCount_;
  untilFrame_ = plan_->hopSize();
  return true;
}

void SpectralSynthesis::prepare(std::shared_ptr<const SpectralPlan> plan) {
  if (!plan) throw std::invalid_argument("POG3 synthesis requires a spectral plan");
  plan_ = std::move(plan);
  scratch_.resize(plan_->frameSize());
  overlap_.resize(plan_->frameSize() * 2);
  reset();
}

void SpectralSynthesis::reset() noexcept {
  std::fill(scratch_.begin(), scratch_.end(), std::complex<float>{});
  std::fill(overlap_.begin(), overlap_.end(), 0.0f);
  read_ = 0;
}

float SpectralSynthesis::pop() noexcept {
  if (!plan_) return 0;
  const float output = overlap_[read_];
  overlap_[read_] = 0;
  read_ = (read_ + 1) & (overlap_.size() - 1);
  return std::isfinite(output) ? output : 0.0f;
}

bool SpectralSynthesis::addFrame(std::span<const std::complex<float>> spectrum, std::size_t startOffset) noexcept {
  if (!plan_ || spectrum.size() != scratch_.size() || startOffset > scratch_.size()) return false;
  // Validate the entire frame before touching OLA; a partially invalid frame
  // must not emit the valid part or contaminate otherwise healthy histories.
  for (const auto value : spectrum)
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) return false;
  std::copy(spectrum.begin(), spectrum.end(), scratch_.begin());
  plan_->transform(scratch_, true);
  const auto window = plan_->synthesisWindow();
  // Finite input bins can still overflow during inverse butterflies or OLA.
  // Reject the whole frame, preserving existing output, before adding any part.
  for (std::size_t i = 0; i < scratch_.size(); ++i) {
    const float candidate = overlap_[(read_ + startOffset + i) & (overlap_.size() - 1)]
      + scratch_[i].real() * window[i];
    if (!std::isfinite(scratch_[i].real()) || !std::isfinite(scratch_[i].imag())
        || !std::isfinite(candidate)) return false;
  }
  for (std::size_t i = 0; i < scratch_.size(); ++i)
    overlap_[(read_ + startOffset + i) & (overlap_.size() - 1)] += scratch_[i].real() * window[i];
  return true;
}

} // namespace ardor::pog3
