#include "equalizer/ParametricEqProcessor.h"

#include <algorithm>
#include <cmath>

namespace ardor {

namespace {

bool isFinite(const EqBandParams& band)
{
  return std::isfinite(band.frequencyHz) && std::isfinite(band.q) && std::isfinite(band.gainDb);
}

EqBandParams clampBand(EqBandParams band)
{
  band.frequencyHz = std::clamp(band.frequencyHz, kEqMinimumFrequencyHz, kEqMaximumFrequencyHz);
  band.q = std::clamp(band.q, kEqMinimumQ, kEqMaximumQ);
  band.gainDb = std::clamp(band.gainDb, kEqMinimumGainDb, kEqMaximumGainDb);
  return band;
}

} // namespace

float ParametricEqProcessor::FilterState::process(float input, const BiquadCoefficients& coefficients)
{
  const float output = coefficients.b0 * input + z1;
  z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
  z2 = coefficients.b2 * input - coefficients.a2 * output;
  return output;
}

bool ParametricEqProcessor::configure(const ParametricEqParams& params, float sampleRate, std::string& error)
{
  error.clear();
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
    error = "parametric EQ sample rate must be finite and positive";
    return false;
  }

  sampleRate_ = sampleRate;
  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    const EqBandParams band = isFinite(params.bands[i])
      ? clampBand(params.bands[i]) : defaultParametricEqBand(i);
    targets_[i].enabled.store(band.enabled, std::memory_order_relaxed);
    targets_[i].frequencyHz.store(band.frequencyHz, std::memory_order_relaxed);
    targets_[i].q.store(band.q, std::memory_order_relaxed);
    targets_[i].gainDb.store(band.gainDb, std::memory_order_relaxed);
    current_[i] = band;
    coefficients_[i] = makePeakingEq(sampleRate_, band.frequencyHz, band.q,
                                     band.enabled ? band.gainDb : 0.0f);
  }
  configured_ = true;
  reset();
  return true;
}

bool ParametricEqProcessor::setBandTarget(std::size_t index, const EqBandParams& params)
{
  if (index >= kParametricEqBandCount || !isFinite(params)) {
    return false;
  }

  const EqBandParams band = clampBand(params);
  targets_[index].enabled.store(band.enabled, std::memory_order_relaxed);
  targets_[index].frequencyHz.store(band.frequencyHz, std::memory_order_relaxed);
  targets_[index].q.store(band.q, std::memory_order_relaxed);
  targets_[index].gainDb.store(band.gainDb, std::memory_order_relaxed);
  return true;
}

void ParametricEqProcessor::updateCoefficients(std::size_t frames)
{
  if (!configured_ || frames == 0) {
    return;
  }

  const float elapsed = static_cast<float>(frames) / sampleRate_;
  const float alpha = 1.0f - std::exp(-elapsed / 0.015f);
  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    const float targetFrequency = targets_[i].frequencyHz.load(std::memory_order_relaxed);
    const float targetQ = targets_[i].q.load(std::memory_order_relaxed);
    const float targetGain = targets_[i].enabled.load(std::memory_order_relaxed)
      ? targets_[i].gainDb.load(std::memory_order_relaxed) : 0.0f;
    current_[i].enabled = targets_[i].enabled.load(std::memory_order_relaxed);
    current_[i].frequencyHz = std::exp(std::log(current_[i].frequencyHz)
      + (std::log(targetFrequency) - std::log(current_[i].frequencyHz)) * alpha);
    current_[i].q += (targetQ - current_[i].q) * alpha;
    current_[i].gainDb += (targetGain - current_[i].gainDb) * alpha;
    coefficients_[i] = makePeakingEq(sampleRate_, current_[i].frequencyHz,
                                     current_[i].q, current_[i].gainDb);
  }
}

void ParametricEqProcessor::processPrepared(float& left, float& right)
{
  if (!configured_) {
    return;
  }

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    left = states_[i][0].process(left, coefficients_[i]);
    right = states_[i][1].process(right, coefficients_[i]);
  }
}

void ParametricEqProcessor::process(float& left, float& right)
{
  if (scalarSamplesUntilUpdate_ == 0) {
    updateCoefficients(64);
    scalarSamplesUntilUpdate_ = 64;
  }
  --scalarSamplesUntilUpdate_;
  processPrepared(left, right);
}

void ParametricEqProcessor::processBlock(const float* inputLeft, const float* inputRight,
                                         float* outputLeft, float* outputRight, std::size_t frames)
{
  if (frames == 0) {
    return;
  }

  updateCoefficients(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    float left = inputLeft[i];
    float right = inputRight[i];
    processPrepared(left, right);
    outputLeft[i] = left;
    outputRight[i] = right;
  }
}

void ParametricEqProcessor::reset()
{
  if (configured_) {
    for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
      current_[i].enabled = targets_[i].enabled.load(std::memory_order_relaxed);
      current_[i].frequencyHz = targets_[i].frequencyHz.load(std::memory_order_relaxed);
      current_[i].q = targets_[i].q.load(std::memory_order_relaxed);
      current_[i].gainDb = current_[i].enabled
        ? targets_[i].gainDb.load(std::memory_order_relaxed) : 0.0f;
      coefficients_[i] = makePeakingEq(sampleRate_, current_[i].frequencyHz,
                                       current_[i].q, current_[i].gainDb);
    }
  }
  for (auto& channels : states_) {
    for (auto& state : channels) {
      state = {};
    }
  }
  scalarSamplesUntilUpdate_ = 0;
}

} // namespace ardor
