#pragma once

#include "dsp/RealtimeFft.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <vector>

namespace ardor::pog3 {

// Immutable, shared by an analysis and all of its voice renderers. Construct
// and destroy off the audio thread. FFTs retain full complex workspaces, with
// 1/N inverse gain. Real/Hermitian inputs use in-place real FFTW plans;
// general complex input retains complex transforms. FFTW planning/destruction
// serialize; execution never locks.
class SpectralPlan {
public:
  SpectralPlan(std::size_t frameSize, std::size_t hopSize);
  std::size_t frameSize() const noexcept { return window_.size(); }
  std::size_t hopSize() const noexcept { return hopSize_; }
  std::span<const float> analysisWindow() const noexcept { return window_; }
  std::span<const float> synthesisWindow() const noexcept { return synthesis_; }
  void transform(std::vector<std::complex<float>>& values, bool inverse) const;

private:
  friend class SpectralSynthesis;
  // Consumed synthesis scratch may stay packed after a real inverse; generic
  // transform callers still receive the complete complex-vector representation.
  bool inverseForSynthesis(std::vector<std::complex<float>>& values) const noexcept;
  // Only the three production resolutions use FFTW. Plans are immutable and
  // shared; each analysis/renderer supplies its own mutable execution buffer.
  struct FftwPlans;
  std::shared_ptr<const FftwPlans> fftw_;
  RealtimeFft fft_;
  std::size_t hopSize_;
  std::vector<float> window_;
  std::vector<float> synthesis_;
};

class SpectralAnalysis {
public:
  // Preparation allocates. All subsequent push/reset calls keep capacities.
  void prepare(std::shared_ptr<const SpectralPlan> plan, bool deferTransform = false);
  void reset() noexcept;
  // True once per hop. By default a frame ends at the sample just pushed.
  // With deferTransform (H >= 2), freeze the same window at its boundary and
  // transform it on the following push, before consuming that next sample.
  // Startup uses zero history. spectrum() is empty while a transform is pending;
  // otherwise its view lasts until the next window boundary, not indefinitely.
  bool push(float sample) noexcept;
  std::span<const std::complex<float>> spectrum() const noexcept {
    return pending_ ? std::span<const std::complex<float>>{} : spectrum_;
  }
  std::size_t frameCount() const noexcept { return frameCount_; }

private:
  std::shared_ptr<const SpectralPlan> plan_;
  std::vector<float> history_;
  std::vector<std::complex<float>> spectrum_;
  std::size_t write_ = 0;
  std::size_t untilFrame_ = 0;
  std::size_t frameCount_ = 0;
  bool deferTransform_ = false, pending_ = false;
};

class SpectralSynthesis {
public:
  // Renderers own mutable FFT scratch. Bounded staging permits an N+H ring.
  void prepare(std::shared_ptr<const SpectralPlan> plan, bool externalScratch = false,
               std::size_t maximumStartOffset = std::numeric_limits<std::size_t>::max());
  void reset() noexcept;
  // Call pop() once per host sample BEFORE analysis.push(). If that push
  // completes a frame, addFrame() schedules its first sample for the next
  // pop. Identity analysis/synthesis therefore has exactly N samples delay.
  float pop() noexcept;
  // Full conjugate-symmetric spectrum. Scratch and OLA storage are preallocated.
  // A bad span is rejected without corrupting existing overlap-add history.
  // startOffset delays the window relative to the next pop, enabling bounded
  // job staging. It must be <= N so the complete window fits the OLA ring.
  bool addFrame(std::span<const std::complex<float>> spectrum, std::size_t startOffset = 0) noexcept;
  // Consumes caller-owned scratch; transform/rejection may modify the vector.
  // OLA remains transactional, including inverse/accumulation overflow.
  bool addFrameInPlace(std::vector<std::complex<float>>& spectrum, std::size_t startOffset = 0) noexcept;

private:
  std::shared_ptr<const SpectralPlan> plan_;
  std::vector<std::complex<float>> scratch_;
  std::vector<float> overlap_;
  std::size_t read_ = 0, maximumStartOffset_ = 0;
  bool synthesize(std::vector<std::complex<float>>& scratch, std::size_t startOffset) noexcept;
};

} // namespace ardor::pog3
