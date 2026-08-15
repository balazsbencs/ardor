#include "dsp/IrReverbProcessor.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kMixSmoothing = 0.0005f;

float onePoleCoeff(float cutoffHz, float sampleRate)
{
  if (!(cutoffHz > 0.0f) || !(sampleRate > 0.0f)) return 1.0f;
  const float k = 1.0f - std::exp(-kTwoPi * cutoffHz / sampleRate);
  return std::clamp(k, 0.00001f, 1.0f);
}

} // namespace

bool IrReverbProcessor::load(std::vector<float> left, std::vector<float> right,
                             float sampleRate, std::string& error)
{
  if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate)) {
    error = "convolution reverb needs a positive sample rate";
    return false;
  }
  if (left.empty()) {
    error = "convolution reverb needs a non-empty impulse";
    return false;
  }
  sampleRate_ = sampleRate;

  const std::size_t maxFrames =
      static_cast<std::size_t>(MAX_IMPULSE_SECONDS * sampleRate);
  if (left.size() > maxFrames) left.resize(maxFrames);
  if (right.size() > maxFrames) right.resize(maxFrames);
  // A mono impulse drives both channels; the reverb is still stereo because the
  // two convolvers see different input.
  if (right.empty()) right = left;

  impulseFrames_ = std::max(left.size(), right.size());
  left_.load(std::move(left), PARTITION_FRAMES);
  right_.load(std::move(right), PARTITION_FRAMES);

  // Room for the largest pre-delay the control offers, plus a guard sample.
  const std::size_t preDelayCapacity =
      static_cast<std::size_t>(0.5f * sampleRate_) + 2;
  preLeft_.assign(preDelayCapacity, 0.0f);
  preRight_.assign(preDelayCapacity, 0.0f);
  preWrite_ = 0;

  updateFilters();
  lowCutL_.reset(); lowCutR_.reset();
  highCutL_.reset(); highCutR_.reset();
  mix_ = mixTarget_;
  level_ = levelTarget_;
  loaded_ = true;
  error.clear();
  return true;
}

void IrReverbProcessor::reset()
{
  left_.reset();
  right_.reset();
  std::fill(preLeft_.begin(), preLeft_.end(), 0.0f);
  std::fill(preRight_.begin(), preRight_.end(), 0.0f);
  preWrite_ = 0;
  lowCutL_.reset(); lowCutR_.reset();
  highCutL_.reset(); highCutR_.reset();
  mix_ = mixTarget_;
  level_ = levelTarget_;
}

void IrReverbProcessor::setMix(float mix)
{
  mixTarget_ = std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 0.0f;
}

void IrReverbProcessor::setLevelDb(float levelDb)
{
  if (!std::isfinite(levelDb)) levelDb = 0.0f;
  levelDb = std::clamp(levelDb, -60.0f, 12.0f);
  levelTarget_ = levelDb <= -60.0f ? 0.0f : std::pow(10.0f, levelDb / 20.0f);
}

void IrReverbProcessor::setPreDelayMs(float milliseconds)
{
  if (!std::isfinite(milliseconds) || milliseconds < 0.0f) milliseconds = 0.0f;
  milliseconds = std::min(milliseconds, 500.0f);
  const std::size_t samples =
      static_cast<std::size_t>(milliseconds * 0.001f * sampleRate_);
  preDelaySamples_ = preLeft_.empty() ? 0 : std::min(samples, preLeft_.size() - 1);
}

void IrReverbProcessor::setLowCutHz(float hz)
{
  lowCutHz_ = std::isfinite(hz) ? std::clamp(hz, LOW_CUT_MIN_HZ, LOW_CUT_MAX_HZ) : LOW_CUT_MIN_HZ;
  updateFilters();
}

void IrReverbProcessor::setHighCutHz(float hz)
{
  highCutHz_ = std::isfinite(hz) ? std::clamp(hz, HIGH_CUT_MIN_HZ, HIGH_CUT_MAX_HZ) : HIGH_CUT_MAX_HZ;
  updateFilters();
}

void IrReverbProcessor::updateFilters()
{
  lowCutActive_ = lowCutHz_ > LOW_CUT_MIN_HZ;
  highCutActive_ = highCutHz_ < HIGH_CUT_MAX_HZ;
  const float lowCut = onePoleCoeff(lowCutHz_, sampleRate_);
  const float highCut = onePoleCoeff(highCutHz_, sampleRate_);
  lowCutL_.coeff = lowCut;
  lowCutR_.coeff = lowCut;
  highCutL_.coeff = highCut;
  highCutR_.coeff = highCut;
}

std::size_t IrReverbProcessor::tailFrames() const noexcept
{
  return loaded_ ? impulseFrames_ + PARTITION_FRAMES : 0;
}

StereoSample IrReverbProcessor::process(StereoSample input)
{
  if (!loaded_) return input;

  // Pre-delay ahead of the convolver, so its buffer only spans the extra delay.
  // Always run the line, even at zero delay: skipping the write would leave
  // stale audio behind for the control to uncover when it is raised again.
  preLeft_[preWrite_] = input.left;
  preRight_[preWrite_] = input.right;
  const std::size_t read =
      (preWrite_ + preLeft_.size() - preDelaySamples_) % preLeft_.size();
  const float sendL = preLeft_[read];
  const float sendR = preRight_[read];
  preWrite_ = (preWrite_ + 1) % preLeft_.size();

  // The convolver buffers internally and spreads its own work, so this is a
  // plain per-sample call from here.
  float wetL = left_.process(sendL);
  float wetR = right_.process(sendR);

  // Shape the tail, not the dry signal.
  if (lowCutActive_) {
    wetL = lowCutL_.highPass(wetL);
    wetR = lowCutR_.highPass(wetR);
  }
  if (highCutActive_) {
    wetL = highCutL_.lowPass(wetL);
    wetR = highCutR_.lowPass(wetR);
  }

  mix_ += kMixSmoothing * (mixTarget_ - mix_);
  level_ += kMixSmoothing * (levelTarget_ - level_);

  if (!std::isfinite(wetL)) wetL = 0.0f;
  if (!std::isfinite(wetR)) wetR = 0.0f;

  const float dry = 1.0f - mix_;
  return StereoSample{
      (input.left * dry + wetL * mix_) * level_,
      (input.right * dry + wetR * mix_) * level_,
  };
}

} // namespace ardor
