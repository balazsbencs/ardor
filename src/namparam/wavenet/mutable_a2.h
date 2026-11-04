// SPDX-License-Identifier: MIT
#pragma once
#include "NAM/dsp.h"
#include "NAM/wavenet/model.h"
#include <span>

namespace nam::wavenet {
// Exact 16-channel A2 topology, with prepared histories and mutable weights.
// The weights and audio contract are identical to the generic WaveNet. ARM64
// uses a four-frame NEON kernel; other hosts use the same layout with scalar math.
class MutableA2 final : public DSP {
public:
  static bool supports(const WaveNetConfig &config);
  MutableA2(std::span<const float> weights, double sampleRate);
  ~MutableA2() override;
  // True when a generated delta can change a dilated convolution kernel.
  static bool overlapsConvolution(size_t offset, size_t count) noexcept;
  void setWeights(std::span<const float> weights, bool updateConvolutions = true);
  void process(NAM_SAMPLE **input, NAM_SAMPLE **output, int frames) override;
  int GetPrewarmSamples() override;

protected:
  void SetMaxBufferSize(int frames) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace nam::wavenet
