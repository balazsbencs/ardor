#include "daisyfx/pog3/SpectralFrameStream.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <mutex>
#include <stdexcept>

#include <fftw3.h>
#include <utility>

namespace ardor::pog3 {

namespace {
// FFTW's planner and destruction mutate global state. This mutex is used only
// during preparation/destruction; execution never acquires it. Other FFTW
// users added to Ardor must use the same serialization policy.
std::mutex& fftwPlannerMutex() {
  static std::mutex mutex;
  return mutex;
}
} // namespace

struct SpectralPlan::FftwPlans {
  fftwf_plan forward = nullptr, inverse = nullptr;
  fftwf_plan unalignedForward = nullptr, unalignedInverse = nullptr;
  int alignment = 0;

  explicit FftwPlans(std::size_t n) {
    // Planning may overwrite its input. Ordinary vector storage also gives
    // the planner the alignment used by our analysis/renderer workspaces.
    std::vector<std::complex<float>> dummy(n);
    auto* data = reinterpret_cast<fftwf_complex*>(dummy.data());
    std::lock_guard lock(fftwPlannerMutex());
    alignment = fftwf_alignment_of(reinterpret_cast<float*>(dummy.data()));
    // Default MEASURE can select dft-buffered plans that allocate/free an
    // execution buffer on every transform. Exclude that solver explicitly.
    // The C-allocation regression checks actual execution, not just C++ new.
    constexpr unsigned flags = FFTW_MEASURE | FFTW_NO_BUFFERING;
    forward = fftwf_plan_dft_1d(static_cast<int>(n), data, data, FFTW_FORWARD, flags);
    inverse = fftwf_plan_dft_1d(static_cast<int>(n), data, data, FFTW_BACKWARD, flags);
    // The public vector API permits a different alignment class. Prepare a
    // scalar-compatible alternative now, instead of copying, allocating or
    // replanning on the callback. Normal aligned execution retains SIMD.
    unalignedForward = fftwf_plan_dft_1d(static_cast<int>(n), data, data, FFTW_FORWARD,
                                        FFTW_ESTIMATE | FFTW_UNALIGNED | FFTW_NO_BUFFERING);
    unalignedInverse = fftwf_plan_dft_1d(static_cast<int>(n), data, data, FFTW_BACKWARD,
                                        FFTW_ESTIMATE | FFTW_UNALIGNED | FFTW_NO_BUFFERING);
    if (!forward || !inverse || !unalignedForward || !unalignedInverse) {
      destroy();
      throw std::runtime_error("POG3 FFTW planning failed");
    }
  }
  ~FftwPlans() {
    std::lock_guard lock(fftwPlannerMutex());
    destroy();
  }
  FftwPlans(const FftwPlans&) = delete;
  FftwPlans& operator=(const FftwPlans&) = delete;

  void destroy() noexcept {
    for (auto plan : {forward, inverse, unalignedForward, unalignedInverse})
      if (plan) fftwf_destroy_plan(plan);
    // Never call fftwf_cleanup(): it invalidates other live plans.
  }
  void execute(std::vector<std::complex<float>>& values, bool backwards) const noexcept {
    auto* data = reinterpret_cast<fftwf_complex*>(values.data());
    // FFTW 3.3.10 alignment_of is pure address arithmetic (kernel/align.c),
    // with no planner state, allocation or synchronization.
    const bool aligned = fftwf_alignment_of(reinterpret_cast<float*>(values.data())) == alignment;
    const auto plan = backwards ? (aligned ? inverse : unalignedInverse)
                                : (aligned ? forward : unalignedForward);
    fftwf_execute_dft(plan, data, data);
    if (backwards) {
      const float scale = 1.0f / static_cast<float>(values.size());
      for (auto& value : values) value *= scale;
    }
  }
};

SpectralPlan::SpectralPlan(std::size_t frameSize, std::size_t hopSize) : hopSize_(hopSize) {
  if (frameSize < 32 || frameSize > 32768 || (frameSize & (frameSize - 1)) != 0
      || hopSize == 0 || hopSize > frameSize / 2 || frameSize % hopSize != 0)
    throw std::invalid_argument("POG3 spectral plan requires a power-of-two frame and a dividing hop <= N/2");
  constexpr double kTwoPi = 6.2831853071795864769;
  if (frameSize == 1024 || frameSize == 2048 || frameSize == 4096)
    fftw_ = std::make_shared<const FftwPlans>(frameSize);
  else fft_.prepare(frameSize);
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
  if (fftw_) fftw_->execute(values, inverse);
  else fft_.transform(values, inverse);
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
