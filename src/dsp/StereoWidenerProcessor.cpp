#include "dsp/StereoWidenerProcessor.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ardor {

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr float kSmoothing = 0.0008f;
} // namespace

bool StereoWidenerProcessor::prepare(float sampleRate, std::string& error)
{
  if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate)) {
    error = "stereo widener needs a positive sample rate";
    return false;
  }
  sampleRate_ = sampleRate;
  const std::size_t capacity =
      static_cast<std::size_t>(MAX_DELAY_MS * 0.001f * sampleRate_) + 2;
  sideDelay_.assign(capacity, 0.0f);
  reset();
  error.clear();
  return true;
}

void StereoWidenerProcessor::reset()
{
  std::fill(sideDelay_.begin(), sideDelay_.end(), 0.0f);
  write_ = 0;
  bassState_ = 0.0f;
  width_ = widthTarget_;
  level_ = levelTarget_;
}

void StereoWidenerProcessor::setWidth(float width)
{
  widthTarget_ = std::isfinite(width) ? std::clamp(width, 0.0f, 2.0f) : 1.0f;
}

void StereoWidenerProcessor::setDelayMs(float milliseconds)
{
  if (!std::isfinite(milliseconds) || milliseconds < 0.0f) milliseconds = 0.0f;
  milliseconds = std::min(milliseconds, MAX_DELAY_MS);
  const std::size_t samples =
      static_cast<std::size_t>(milliseconds * 0.001f * sampleRate_);
  delaySamples_ = sideDelay_.empty() ? 0 : std::min(samples, sideDelay_.size() - 1);
}

void StereoWidenerProcessor::setBassMonoHz(float hz)
{
  if (!std::isfinite(hz) || hz <= 20.0f) {
    // Off, rather than merely gentle: a one-pole at 20 Hz still pulls a little
    // of the low end to the centre.
    bassMonoActive_ = false;
    bassCoeff_ = 0.0f;
    return;
  }
  const float cutoff = std::clamp(hz, 20.0f, 500.0f);
  bassMonoActive_ = true;
  bassCoeff_ = std::clamp(1.0f - std::exp(-kTwoPi * cutoff / sampleRate_), 0.00001f, 1.0f);
}

void StereoWidenerProcessor::setLevelDb(float levelDb)
{
  if (!std::isfinite(levelDb)) levelDb = 0.0f;
  levelDb = std::clamp(levelDb, -24.0f, 12.0f);
  levelTarget_ = std::pow(10.0f, levelDb / 20.0f);
}

StereoSample StereoWidenerProcessor::process(StereoSample input)
{
  const float mid = 0.5f * (input.left + input.right);
  float side = 0.5f * (input.left - input.right);

  if (!sideDelay_.empty()) {
    sideDelay_[write_] = side;
    const std::size_t read =
        (write_ + sideDelay_.size() - delaySamples_) % sideDelay_.size();
    side = sideDelay_[read];
    write_ = (write_ + 1) % sideDelay_.size();
  }

  if (bassMonoActive_) {
    // Remove the low part of the side so bass sits in the centre. The high part
    // keeps whatever width the source had.
    bassState_ += bassCoeff_ * (side - bassState_);
    side -= bassState_;
  }

  width_ += kSmoothing * (widthTarget_ - width_);
  level_ += kSmoothing * (levelTarget_ - level_);
  side *= width_;

  return StereoSample{(mid + side) * level_, (mid - side) * level_};
}

} // namespace ardor
