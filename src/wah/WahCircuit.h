#pragma once

#include "wah/WahTable.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

// Runtime evaluation of the DK model from the precomputed table.
//
// process() runs at the OVERSAMPLED rate and knows nothing about oversampling;
// WahProcessor owns that. Per sample the cost is fixed and contains no branch
// on signal value and no loop whose trip count depends on the signal, which is
// what makes it safe in a SCHED_FIFO callback.
//
// Nothing allocates after load().
class WahCircuit {
public:
  bool load(const std::filesystem::path& path, std::string& error);
  bool loaded() const noexcept { return loaded_; }
  float sampleRate() const noexcept { return table_.header.sampleRate; }

  // Blends the two bracketing precomputed pot positions. Control-rate; cheap
  // enough to call per host-rate sample.
  void setPotPosition(float position);
  float potPosition() const noexcept { return position_; }

  // `input` is the audio sample; the 9 V rail is supplied internally as the
  // model's second input.
  float process(float input);
  void reset();

private:
  void refreshBlend();
  float interpolate(std::size_t port, float p1, float p2) const;

  WahTable table_;
  bool loaded_ = false;

  std::size_t states_ = 0;
  std::size_t ports_ = 0;
  std::size_t inputs_ = 0;

  float position_ = 0.0f;
  std::size_t potLow_ = 0;
  std::size_t potHigh_ = 0;
  float potBlend_ = 0.0f;

  // Blended matrices for the current pot position. Both the matrices and the
  // solution lookup must blend; snapping either one to the nearest position
  // steps the output audibly when the treadle moves slowly.
  std::vector<float> a_;
  std::vector<float> b_;
  std::vector<float> c_;
  std::vector<float> d_;
  std::vector<float> e_;
  std::vector<float> f_;
  std::vector<float> g_;
  std::vector<float> h_;
  std::vector<float> k_;

  std::vector<float> state_;
  std::vector<float> scratch_;
  float supplyVolts_ = 9.0f;
};

} // namespace ardor
