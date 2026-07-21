#include "dsp/Tuner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ardor {

namespace {

// Chromatic guitar range: covers extended-range low strings and notes above
// standard high E while keeping the lag search compact.
constexpr float kMinimumFrequency = 55.0f;
constexpr float kMaximumFrequency = 500.0f;
constexpr float kMinimumRms = 0.0025f;
constexpr float kYinThreshold = 0.16f;
constexpr std::array<const char*, 12> kNoteNames{
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // namespace

TunerAnalyzer::TunerAnalyzer(float sampleRate)
  : inputSampleRate_(std::isfinite(sampleRate) && sampleRate > 0.0f ? sampleRate : 48000.0f),
    analysisSampleRate_(inputSampleRate_ / static_cast<float>(kDownsampleFactor)),
    ring_(kWindowSize, 0.0f),
    window_(kWindowSize, 0.0f),
    difference_(static_cast<std::size_t>(analysisSampleRate_ / kMinimumFrequency) + 2, 0.0f)
{
}

void TunerAnalyzer::reset()
{
  std::fill(ring_.begin(), ring_.end(), 0.0f);
  writePosition_ = 0;
  available_ = 0;
  sinceAnalysis_ = 0;
  downsampleSum_ = 0.0f;
  downsampleCount_ = 0;
  reading_ = {};
}

void TunerAnalyzer::process(const float* samples, std::size_t count)
{
  if (!samples) return;
  for (std::size_t i = 0; i < count; ++i) {
    const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
    downsampleSum_ += sample;
    if (++downsampleCount_ == kDownsampleFactor) {
      pushDownsampled(downsampleSum_ / static_cast<float>(kDownsampleFactor));
      downsampleSum_ = 0.0f;
      downsampleCount_ = 0;
    }
  }
}

void TunerAnalyzer::pushDownsampled(float sample)
{
  ring_[writePosition_] = sample;
  writePosition_ = (writePosition_ + 1) % ring_.size();
  if (available_ < kWindowSize) {
    ++available_;
    if (available_ == kWindowSize) {
      sinceAnalysis_ = 0;
      analyze();
    }
    return;
  }
  if (++sinceAnalysis_ < kHopSize) return;
  sinceAnalysis_ = 0;
  analyze();
}

void TunerAnalyzer::analyze()
{
  float mean = 0.0f;
  for (std::size_t i = 0; i < kWindowSize; ++i) {
    const std::size_t source = (writePosition_ + i) % kWindowSize;
    window_[i] = ring_[source];
    mean += window_[i];
  }
  mean /= static_cast<float>(kWindowSize);

  float energy = 0.0f;
  for (auto& sample : window_) {
    sample -= mean;
    energy += sample * sample;
  }
  const float rms = std::sqrt(energy / static_cast<float>(kWindowSize));
  if (!std::isfinite(rms) || rms < kMinimumRms) {
    publishNoSignal();
    return;
  }

  const std::size_t minTau = static_cast<std::size_t>(analysisSampleRate_ / kMaximumFrequency);
  const std::size_t maxTau = std::min(
    difference_.size() - 2,
    static_cast<std::size_t>(analysisSampleRate_ / kMinimumFrequency));
  std::fill(difference_.begin(), difference_.end(), 0.0f);
  for (std::size_t tau = 1; tau <= maxTau; ++tau) {
    float sum = 0.0f;
    const std::size_t limit = kWindowSize - maxTau;
    for (std::size_t i = 0; i < limit; ++i) {
      const float delta = window_[i] - window_[i + tau];
      sum += delta * delta;
    }
    difference_[tau] = sum;
  }

  float runningSum = 0.0f;
  difference_[0] = 1.0f;
  for (std::size_t tau = 1; tau <= maxTau; ++tau) {
    runningSum += difference_[tau];
    difference_[tau] = runningSum > 0.0f
      ? difference_[tau] * static_cast<float>(tau) / runningSum
      : 1.0f;
  }

  std::size_t bestTau = minTau;
  float bestValue = std::numeric_limits<float>::max();
  for (std::size_t tau = minTau; tau <= maxTau; ++tau) {
    if (difference_[tau] < bestValue) {
      bestValue = difference_[tau];
      bestTau = tau;
    }
    if (difference_[tau] < kYinThreshold) {
      while (tau + 1 <= maxTau && difference_[tau + 1] < difference_[tau]) {
        ++tau;
      }
      bestTau = tau;
      bestValue = difference_[tau];
      break;
    }
  }

  const float confidence = std::clamp(1.0f - bestValue, 0.0f, 1.0f);
  if (confidence < 0.72f || bestTau <= 1 || bestTau >= maxTau) {
    publishNoSignal();
    return;
  }

  const float left = difference_[bestTau - 1];
  const float center = difference_[bestTau];
  const float right = difference_[bestTau + 1];
  const float denominator = left - 2.0f * center + right;
  const float offset = std::fabs(denominator) > 1.0e-9f
    ? std::clamp(0.5f * (left - right) / denominator, -1.0f, 1.0f)
    : 0.0f;
  const float frequency = analysisSampleRate_ / (static_cast<float>(bestTau) + offset);
  if (!std::isfinite(frequency) || frequency < kMinimumFrequency || frequency > kMaximumFrequency) {
    publishNoSignal();
    return;
  }

  const float midi = 69.0f + 12.0f * std::log2(frequency / 440.0f);
  const int nearestMidi = static_cast<int>(std::lround(midi));
  reading_.signalDetected = true;
  reading_.frequencyHz = frequency;
  reading_.cents = std::clamp(100.0f * (midi - static_cast<float>(nearestMidi)), -50.0f, 50.0f);
  reading_.confidence = confidence;
  reading_.note = kNoteNames[static_cast<std::size_t>((nearestMidi % 12 + 12) % 12)];
  reading_.octave = nearestMidi / 12 - 1;
  ++reading_.revision;
}

void TunerAnalyzer::publishNoSignal()
{
  reading_.signalDetected = false;
  reading_.frequencyHz = 0.0f;
  reading_.cents = 0.0f;
  reading_.confidence = 0.0f;
  reading_.note = "--";
  reading_.octave = 0;
  ++reading_.revision;
}

} // namespace ardor
