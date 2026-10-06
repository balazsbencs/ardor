#include "daisyfx/pog3/SpectralFrameStream.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ardor::pog3 {

SpectralPlan::SpectralPlan(std::size_t frameSize, std::size_t hopSize) : hopSize_(hopSize) {
  if (frameSize < 32 || frameSize > 32768 || (frameSize & (frameSize - 1)) != 0
      || hopSize == 0 || hopSize > frameSize / 2 || frameSize % hopSize != 0)
    throw std::invalid_argument("POG3 spectral plan requires a power-of-two frame and a dividing hop <= N/2");
  constexpr double kTwoPi = 6.2831853071795864769;
  if (frameSize > 4096) fft_.prepare(frameSize);
  else {
    // Reversing log2(N) bits fixes 2^ceil(log2(N)/2) palindromic indices.
    // Every other index participates in exactly one swap. Store only those
    // pairs, in the shared FFT's traversal order, with no callback comparison.
    const auto fixed = std::size_t{1} << ((std::countr_zero(frameSize) + 1) / 2);
    fftSwaps_.resize(frameSize - fixed);
    std::size_t position = 0;
    for (std::size_t i = 1, j = 0; i < frameSize; ++i) {
      std::size_t bit = frameSize >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        fftSwaps_[position++] = static_cast<std::uint16_t>(i);
        fftSwaps_[position++] = static_cast<std::uint16_t>(j);
      }
    }
    assert(position == fftSwaps_.size());
    // Sum of half-stage lengths is N-1; stage len starts at len/2-1.
    // Use the original largest-table index/arithmetic to retain each twiddle
    // bit, rather than independently approximating smaller-stage angles.
    fftTwiddles_.resize(frameSize - 1);
    for (std::size_t len = 2; len <= frameSize; len <<= 1) {
      const auto stride = frameSize / len;
      for (std::size_t j = 0; j < len / 2; ++j) {
        const double angle = -kTwoPi * static_cast<double>(j * stride) / static_cast<double>(frameSize);
        fftTwiddles_[len / 2 - 1 + j] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
      }
    }
  }
  window_.resize(frameSize);
  synthesis_.resize(frameSize);
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
  if (fft_.size()) fft_.transform(values, inverse);
  else if (inverse) transformPrepared<true>(values);
  else transformPrepared<false>(values);
}

template<bool Inverse>
void SpectralPlan::transformPrepared(std::vector<std::complex<float>>& values) const {
  for (std::size_t p = 0; p < fftSwaps_.size(); p += 2)
    std::swap(values[fftSwaps_[p]], values[fftSwaps_[p + 1]]);
  const auto n = values.size();
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const auto* weights = fftTwiddles_.data() + len / 2 - 1;
    for (std::size_t i = 0; i < n; i += len) {
      for (std::size_t j = 0; j < len / 2; ++j) {
        auto w = weights[j];
        if constexpr (Inverse) w = std::conj(w);
        const auto u = values[i + j];
        const auto v = values[i + j + len / 2] * w;
        values[i + j] = u + v;
        values[i + j + len / 2] = u - v;
      }
    }
  }
  if constexpr (Inverse) {
    const float scale = 1.0f / static_cast<float>(n);
    for (auto& value : values) value *= scale;
  }
}

void SpectralAnalysis::prepare(std::shared_ptr<const SpectralPlan> plan, bool deferTransform) {
  if (!plan) throw std::invalid_argument("POG3 analysis requires a spectral plan");
  if (deferTransform && plan->hopSize() < 2)
    throw std::invalid_argument("POG3 deferred analysis requires a hop of at least two samples");
  plan_ = std::move(plan);
  deferTransform_ = deferTransform;
  history_.resize(plan_->frameSize());
  spectrum_.resize(plan_->frameSize());
  reset();
}

void SpectralAnalysis::reset() noexcept {
  std::fill(history_.begin(), history_.end(), 0.0f);
  std::fill(spectrum_.begin(), spectrum_.end(), std::complex<float>{});
  write_ = frameCount_ = 0;
  pending_ = false;
  untilFrame_ = plan_ ? plan_->hopSize() : 0;
}

bool SpectralAnalysis::push(float sample) noexcept {
  if (!plan_) return false;
  bool ready = false;
  if (pending_) {
    plan_->transform(spectrum_, false);
    ++frameCount_;
    pending_ = false;
    ready = true;
  }
  // A nonfinite input is never allowed to poison future windows.
  history_[write_] = std::isfinite(sample) ? sample : 0.0f;
  write_ = (write_ + 1) & (history_.size() - 1);
  if (--untilFrame_ != 0) return ready;
  const auto window = plan_->analysisWindow();
  for (std::size_t i = 0; i < spectrum_.size(); ++i)
    spectrum_[i] = {history_[(write_ + i) & (history_.size() - 1)] * window[i], 0};
  untilFrame_ = plan_->hopSize();
  if (deferTransform_) pending_ = true;
  else {
    plan_->transform(spectrum_, false);
    ++frameCount_;
    ready = true;
  }
  return ready;
}

void SpectralSynthesis::prepare(std::shared_ptr<const SpectralPlan> plan, bool externalScratch,
                                std::size_t maximumStartOffset) {
  if (!plan) throw std::invalid_argument("POG3 synthesis requires a spectral plan");
  if (maximumStartOffset == std::numeric_limits<std::size_t>::max()) maximumStartOffset = plan->frameSize();
  if (maximumStartOffset > plan->frameSize()) throw std::invalid_argument("POG3 synthesis staging exceeds N");
  plan_ = std::move(plan);
  maximumStartOffset_ = maximumStartOffset;
  if (externalScratch) std::vector<std::complex<float>>{}.swap(scratch_);
  else scratch_.resize(plan_->frameSize());
  overlap_.resize(plan_->frameSize() + maximumStartOffset_);
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
  if (++read_ == overlap_.size()) read_ = 0;
  return std::isfinite(output) ? output : 0.0f;
}

bool SpectralSynthesis::addFrame(std::span<const std::complex<float>> spectrum, std::size_t startOffset) noexcept {
  if (!plan_ || scratch_.size() != plan_->frameSize() || spectrum.size() != scratch_.size()
      || startOffset > maximumStartOffset_) return false;
  // Validate the entire frame before touching OLA; a partially invalid frame
  // must not emit the valid part or contaminate otherwise healthy histories.
  for (const auto value : spectrum)
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) return false;
  std::copy(spectrum.begin(), spectrum.end(), scratch_.begin());
  return synthesize(scratch_, startOffset);
}

bool SpectralSynthesis::addFrameInPlace(std::vector<std::complex<float>>& spectrum, std::size_t startOffset) noexcept {
  if (!plan_ || spectrum.size() != plan_->frameSize() || startOffset > maximumStartOffset_) return false;
  for (const auto value : spectrum)
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) return false;
  return synthesize(spectrum, startOffset);
}

bool SpectralSynthesis::synthesize(std::vector<std::complex<float>>& scratch, std::size_t startOffset) noexcept {
  plan_->transform(scratch, true);
  const auto window = plan_->synthesisWindow();
  // Finite input bins can still overflow during inverse butterflies or OLA.
  // Reject the whole frame, preserving existing output, before adding any part.
  const auto begin = read_ + startOffset < overlap_.size() ? read_ + startOffset : read_ + startOffset - overlap_.size();
  auto position = begin;
  for (std::size_t i = 0; i < scratch.size(); ++i) {
    const float candidate = overlap_[position] + scratch[i].real() * window[i];
    if (!std::isfinite(scratch[i].real()) || !std::isfinite(scratch[i].imag())
        || !std::isfinite(candidate)) return false;
    if (++position == overlap_.size()) position = 0;
  }
  position = begin;
  for (std::size_t i = 0; i < scratch.size(); ++i) {
    overlap_[position] += scratch[i].real() * window[i];
    if (++position == overlap_.size()) position = 0;
  }
  return true;
}

} // namespace ardor::pog3
