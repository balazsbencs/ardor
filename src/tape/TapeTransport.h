#pragma once

#include "daisyfx/DaisyFxProcessor.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ardor {

// Wow, flutter, scrape flutter and hiss.
//
// One transport drives both channels. A stereo pair runs on one reel past one
// capstan, so wow and flutter are correlated between the channels by physics;
// independent modulation would tear the stereo image apart. Hiss is the
// opposite case — tape noise is uncorrelated — so each channel gets its own
// generator.
//
// Full-scale flutter is deliberately about ten times a real Studer A800, which
// holds wow and flutter near 0.03% DIN weighted. At the real figure the control
// would be inaudible across its whole range and would read as broken. The
// realistic setting is near 0.1.
class TapeTransport {
public:
  // Below this the noise generator is skipped entirely rather than run at a
  // low level, so a preset with hiss off is exactly silent.
  static constexpr float kHissOffDb = -120.0f;

  // A delay line cannot read ahead of its write pointer, so this class cannot
  // be delay-neutral: the modulation has to swing both ways around something.
  // What it is instead is predictable — at zero flutter the delay is exactly
  // this many frames, so the owning block subtracts a known constant from its
  // dry path rather than guessing. Comfortably above the largest swing the
  // components below can produce.
  static constexpr std::size_t kNominalDelay = 48;

  void configure(float sampleRate);
  void setFlutter(float depth);   // 0..1
  void setHissDb(float db);       // kHissOffDb..-60
  void reset();
  StereoSample process(StereoSample input);

private:
  // Peak pitch deviation each component contributes at full scale, as a
  // fraction. A sinusoidal delay modulation of amplitude A at rate f gives a
  // peak pitch deviation of A*2*pi*f, so the delay amplitudes are these
  // figures divided by their own 2*pi*f.
  struct Component {
    float rateHz;
    float pitchDeviation;
  };
  static constexpr std::array<Component, 3> kComponents = {{
    {0.7f, 0.0015f},  // wow
    {6.0f, 0.0010f},  // flutter
    {11.0f, 0.0005f}, // flutter, second mode
  }};
  static constexpr float kScrapeRateHz = 250.0f;
  static constexpr float kScrapeDeviation = 0.0002f;

  static constexpr std::size_t kBufferSize = 256; // power of two, masked index
  static constexpr std::size_t kBufferMask = kBufferSize - 1U;

  float readDelayed(const std::array<float, kBufferSize>& buffer, float delay) const;
  float nextModulation();
  static float nextNoise(std::uint32_t& state);

  float sampleRate_ = 48000.0f;
  float flutter_ = 0.0f;
  float hissGain_ = 0.0f;
  bool hissEnabled_ = false;

  std::array<float, 3> phase_{};
  std::array<float, 3> phaseStep_{};
  std::array<float, 3> delayAmplitude_{};
  float scrapeAmplitude_ = 0.0f;
  float scrapeState_ = 0.0f;
  float scrapeCoefficient_ = 0.0f;

  std::array<float, kBufferSize> left_{};
  std::array<float, kBufferSize> right_{};
  std::size_t write_ = 0;

  // Separate seeds so the two channels' noise is independent.
  std::uint32_t leftNoise_ = 0x9E3779B9u;
  std::uint32_t rightNoise_ = 0x85EBCA6Bu;
  std::uint32_t scrapeNoise_ = 0xC2B2AE35u;
};

} // namespace ardor
