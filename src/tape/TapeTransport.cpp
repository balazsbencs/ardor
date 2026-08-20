#include "tape/TapeTransport.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {
constexpr float kTwoPi = 6.28318530718f;
} // namespace

void TapeTransport::configure(float sampleRate)
{
  sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f) ? sampleRate : 48000.0f;

  for (std::size_t i = 0; i < kComponents.size(); ++i) {
    phaseStep_[i] = kTwoPi * kComponents[i].rateHz / sampleRate_;
    // A sinusoidal delay modulation of amplitude A at rate f produces a peak
    // pitch deviation of A * 2*pi*f, so invert that for the amplitude needed.
    delayAmplitude_[i] =
      kComponents[i].pitchDeviation * sampleRate_ / (kTwoPi * kComponents[i].rateHz);
  }
  scrapeAmplitude_ = kScrapeDeviation * sampleRate_ / (kTwoPi * kScrapeRateHz);
  // One-pole low pass that shapes white noise into scrape flutter.
  scrapeCoefficient_ = 1.0f - std::exp(-kTwoPi * kScrapeRateHz / sampleRate_);
  reset();
}

void TapeTransport::setFlutter(float depth)
{
  flutter_ = std::clamp(depth, 0.0f, 1.0f);
}

void TapeTransport::setHissDb(float db)
{
  hissEnabled_ = db > kHissOffDb;
  hissGain_ = hissEnabled_ ? std::pow(10.0f, db / 20.0f) : 0.0f;
}

void TapeTransport::reset()
{
  phase_.fill(0.0f);
  scrapeState_ = 0.0f;
  left_.fill(0.0f);
  right_.fill(0.0f);
  write_ = 0;
  leftNoise_ = 0x9E3779B9u;
  rightNoise_ = 0x85EBCA6Bu;
  scrapeNoise_ = 0xC2B2AE35u;
}

float TapeTransport::nextNoise(std::uint32_t& state)
{
  // xorshift32: cheap, allocation-free and good enough for a noise floor.
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(static_cast<std::int32_t>(state)) * (1.0f / 2147483648.0f);
}

float TapeTransport::nextModulation()
{
  float offset = 0.0f;
  for (std::size_t i = 0; i < kComponents.size(); ++i) {
    offset += delayAmplitude_[i] * std::sin(phase_[i]);
    phase_[i] += phaseStep_[i];
    if (phase_[i] > kTwoPi) phase_[i] -= kTwoPi;
  }
  // Scrape flutter is broadband, not a tone, so it is filtered noise. It draws
  // from its own generator rather than the left channel's: sharing one would
  // correlate the pitch movement with the hiss on one side only.
  scrapeState_ += scrapeCoefficient_ * (nextNoise(scrapeNoise_) - scrapeState_);
  offset += scrapeAmplitude_ * scrapeState_;
  return offset * flutter_;
}

float TapeTransport::readDelayed(const std::array<float, kBufferSize>& buffer, float delay) const
{
  // write_ has already advanced, so the newest sample sits at write_ - 1 and a
  // delay of zero must land there.
  const float position = static_cast<float>(write_ + kBufferSize - 1U) - delay;
  const auto base = static_cast<std::size_t>(position);
  const float fraction = position - static_cast<float>(base);

  // Third-order Lagrange. Linear interpolation would low-pass the signal in
  // step with the modulation, which reads as a wobbling tone control rather
  // than as pitch movement. At fraction zero this returns y1 exactly, which is
  // what makes the delay exactly kNominalDelay when flutter is off.
  // The four taps sit at x = -1, 0, +1, +2 around the read position, and a
  // rising buffer index is a more recent sample, so they run forward from
  // base-1. Reversing them silently mistunes the interpolation.
  const float y0 = buffer[(base - 1U) & kBufferMask];
  const float y1 = buffer[base & kBufferMask];
  const float y2 = buffer[(base + 1U) & kBufferMask];
  const float y3 = buffer[(base + 2U) & kBufferMask];

  const float d1 = fraction - 1.0f;
  const float d2 = fraction - 2.0f;
  const float dp1 = fraction + 1.0f;

  return y0 * (-fraction * d1 * d2 / 6.0f)
       + y1 * (dp1 * d1 * d2 / 2.0f)
       + y2 * (-dp1 * fraction * d2 / 2.0f)
       + y3 * (dp1 * fraction * d1 / 6.0f);
}

StereoSample TapeTransport::process(StereoSample input)
{
  left_[write_] = input.left;
  right_[write_] = input.right;
  write_ = (write_ + 1U) & kBufferMask;

  const float delay = static_cast<float>(kNominalDelay) + nextModulation();
  StereoSample output{readDelayed(left_, delay), readDelayed(right_, delay)};

  if (hissEnabled_) {
    output.left += hissGain_ * nextNoise(leftNoise_);
    output.right += hissGain_ * nextNoise(rightNoise_);
  }
  return output;
}

} // namespace ardor
