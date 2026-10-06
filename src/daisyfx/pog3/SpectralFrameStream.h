#pragma once

#include "dsp/RealtimeFft.h"

#include <complex>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace ardor::pog3 {

// Immutable, shared by an analysis and all of its voice renderers. Construct
// off the audio thread. FFTs use full complex workspaces, with 1/N inverse gain.
class SpectralPlan {
public:
  SpectralPlan(std::size_t frameSize, std::size_t hopSize);
  std::size_t frameSize() const noexcept { return window_.size(); }
  std::size_t hopSize() const noexcept { return hopSize_; }
  std::span<const float> analysisWindow() const noexcept { return window_; }
  std::span<const float> synthesisWindow() const noexcept { return synthesis_; }
  void transform(std::vector<std::complex<float>>& values, bool inverse) const;

private:
  RealtimeFft fft_;
  std::size_t hopSize_;
  std::vector<float> window_;
  std::vector<float> synthesis_;
};

class SpectralAnalysis {
public:
  // Preparation allocates. All subsequent push/reset calls keep capacities.
  void prepare(std::shared_ptr<const SpectralPlan> plan);
  void reset() noexcept;
  // True once per hop. A frame ends at the sample just pushed. Startup uses
  // zero history. spectrum() remains valid until the next completed frame.
  bool push(float sample) noexcept;
  std::span<const std::complex<float>> spectrum() const noexcept { return spectrum_; }
  std::size_t frameCount() const noexcept { return frameCount_; }

private:
  std::shared_ptr<const SpectralPlan> plan_;
  std::vector<float> history_;
  std::vector<std::complex<float>> spectrum_;
  std::size_t write_ = 0;
  std::size_t untilFrame_ = 0;
  std::size_t frameCount_ = 0;
};

class SpectralSynthesis {
public:
  void prepare(std::shared_ptr<const SpectralPlan> plan);
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

private:
  std::shared_ptr<const SpectralPlan> plan_;
  std::vector<std::complex<float>> scratch_;
  std::vector<float> overlap_;
  std::size_t read_ = 0;
};

} // namespace ardor::pog3
