#pragma once

#include "dsp/RealtimeFft.h"

#include <complex>
#include <cstddef>
#include <vector>

namespace ardor {

// Uniformly partitioned convolution with the per-partition work spread evenly
// across the period between block boundaries.
//
// IrConvolver does the whole accumulation at the boundary, which is right for a
// cabinet: a few thousand taps make the burst small. A reverb impulse is long
// enough that the burst dominates. Measured on an Apple-silicon host with a 2 s
// stereo impulse, 64-frame quanta, doing it all at the boundary:
//
//   median block  0.83 us      15 of every 16 blocks did nothing
//   p99           538 us
//   max           658 us       against a 1333 us budget
//
// The burst is 2 x impulse length of complex multiply-accumulates and does not
// depend on partition size — a smaller partition just makes it more frequent.
// Scaled to a Cortex-A72 that overruns the quantum outright.
//
// The way out follows from when each term becomes available. The output for
// block n is the sum over p of H[p] * X[n-p]. Only H[0] * X[n] needs the block
// that just closed; every other term uses history that was already complete
// when the period began. So this class accumulates those older terms a slice at
// a time while the period runs, leaving the boundary with one multiply pass and
// two transforms. Cost per sample becomes near-flat.
class ScheduledConvolver {
public:
  // `impulse` may be any length; `partitionFrames` must be a power of two.
  void load(std::vector<float> impulse, std::size_t partitionFrames);
  void reset();
  bool loaded() const noexcept { return !impulse_.empty(); }

  std::size_t partitionFrames() const noexcept { return partition_; }
  std::size_t impulseFrames() const noexcept { return impulse_.size(); }

  // Feeds one sample and returns one sample, delayed by partitionFrames().
  float process(float input);

private:
  void beginPeriod();
  void advanceSchedule();
  void closeBlock();

  std::vector<float> impulse_;
  std::size_t partition_ = 0;
  std::size_t fftSize_ = 0;
  std::size_t partitionCount_ = 0;
  RealtimeFft fft_;

  std::vector<std::vector<std::complex<float>>> impulseSpectra_;
  std::vector<std::vector<std::complex<float>>> inputSpectra_;
  std::size_t newestInput_ = 0;   // index of the most recently stored input spectrum

  std::vector<std::complex<float>> accumulator_;  // running sum for the next output
  std::vector<std::complex<float>> scratch_;
  std::vector<float> overlap_;
  std::vector<float> inBuffer_;
  std::vector<float> outBuffer_;
  std::size_t fill_ = 0;

  // Spreads partitionCount_ - 1 multiply passes over partition_ samples without
  // needing a division per sample.
  std::size_t schedulePending_ = 0;
  std::size_t scheduleCursor_ = 1;
};

} // namespace ardor
