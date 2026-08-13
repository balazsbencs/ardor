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

bool isFinite(const EqPassFilterParams& filter)
{
  return std::isfinite(filter.frequencyHz) && std::isfinite(filter.q);
}

EqPassFilterParams clampPassFilter(EqPassFilterParams filter)
{
  filter.frequencyHz = std::clamp(filter.frequencyHz, kEqMinimumFrequencyHz, kEqMaximumFrequencyHz);
  filter.q = std::clamp(filter.q, kEqMinimumQ, kEqMaximumQ);
  return filter;
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
  const auto highPass = isFinite(params.highPass)
    ? clampPassFilter(params.highPass) : defaultEqPassFilter(EqPassFilterKind::HighPass);
  const auto lowPass = isFinite(params.lowPass)
    ? clampPassFilter(params.lowPass) : defaultEqPassFilter(EqPassFilterKind::LowPass);
  highPassTarget_.enabled.store(highPass.enabled, std::memory_order_relaxed);
  highPassTarget_.frequencyHz.store(highPass.frequencyHz, std::memory_order_relaxed);
  highPassTarget_.q.store(highPass.q, std::memory_order_relaxed);
  lowPassTarget_.enabled.store(lowPass.enabled, std::memory_order_relaxed);
  lowPassTarget_.frequencyHz.store(lowPass.frequencyHz, std::memory_order_relaxed);
  lowPassTarget_.q.store(lowPass.q, std::memory_order_relaxed);
  currentHighPass_ = highPass;
  currentLowPass_ = lowPass;
  highPassCoefficients_ = makeHighPass(sampleRate_, highPass.frequencyHz, highPass.q);
  lowPassCoefficients_ = makeLowPass(sampleRate_, lowPass.frequencyHz, lowPass.q);
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

bool ParametricEqProcessor::setPassFilterTarget(EqPassFilterKind kind,
                                                const EqPassFilterParams& params)
{
  if (!isFinite(params)) {
    return false;
  }
  const auto filter = clampPassFilter(params);
  auto& target = kind == EqPassFilterKind::HighPass ? highPassTarget_ : lowPassTarget_;
  target.enabled.store(filter.enabled, std::memory_order_relaxed);
  target.frequencyHz.store(filter.frequencyHz, std::memory_order_relaxed);
  target.q.store(filter.q, std::memory_order_relaxed);
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
  const auto updatePassFilter = [this, alpha](AtomicPassFilter& target,
                                               EqPassFilterParams& current,
                                               BiquadCoefficients& coefficients,
                                               float& mix, EqPassFilterKind kind) {
    const float targetFrequency = target.frequencyHz.load(std::memory_order_relaxed);
    const float targetQ = target.q.load(std::memory_order_relaxed);
    current.enabled = target.enabled.load(std::memory_order_relaxed);
    current.frequencyHz = std::exp(std::log(current.frequencyHz)
      + (std::log(targetFrequency) - std::log(current.frequencyHz)) * alpha);
    current.q += (targetQ - current.q) * alpha;
    mix += ((current.enabled ? 1.0f : 0.0f) - mix) * alpha;
    coefficients = kind == EqPassFilterKind::HighPass
      ? makeHighPass(sampleRate_, current.frequencyHz, current.q)
      : makeLowPass(sampleRate_, current.frequencyHz, current.q);
  };
  updatePassFilter(highPassTarget_, currentHighPass_, highPassCoefficients_, highPassMix_,
                   EqPassFilterKind::HighPass);
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
  updatePassFilter(lowPassTarget_, currentLowPass_, lowPassCoefficients_, lowPassMix_,
                   EqPassFilterKind::LowPass);
}

void ParametricEqProcessor::processPrepared(float& left, float& right)
{
  if (!configured_) {
    return;
  }

  const float dryLeft = left;
  const float dryRight = right;
  const float highLeft = highPassStates_[0].process(left, highPassCoefficients_);
  const float highRight = highPassStates_[1].process(right, highPassCoefficients_);
  left = dryLeft + (highLeft - dryLeft) * highPassMix_;
  right = dryRight + (highRight - dryRight) * highPassMix_;

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    left = states_[i][0].process(left, coefficients_[i]);
    right = states_[i][1].process(right, coefficients_[i]);
  }
  const float preLowLeft = left;
  const float preLowRight = right;
  const float lowLeft = lowPassStates_[0].process(left, lowPassCoefficients_);
  const float lowRight = lowPassStates_[1].process(right, lowPassCoefficients_);
  left = preLowLeft + (lowLeft - preLowLeft) * lowPassMix_;
  right = preLowRight + (lowRight - preLowRight) * lowPassMix_;
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
    currentHighPass_.enabled = highPassTarget_.enabled.load(std::memory_order_relaxed);
    currentHighPass_.frequencyHz = highPassTarget_.frequencyHz.load(std::memory_order_relaxed);
    currentHighPass_.q = highPassTarget_.q.load(std::memory_order_relaxed);
    currentLowPass_.enabled = lowPassTarget_.enabled.load(std::memory_order_relaxed);
    currentLowPass_.frequencyHz = lowPassTarget_.frequencyHz.load(std::memory_order_relaxed);
    currentLowPass_.q = lowPassTarget_.q.load(std::memory_order_relaxed);
    highPassCoefficients_ = makeHighPass(sampleRate_, currentHighPass_.frequencyHz, currentHighPass_.q);
    lowPassCoefficients_ = makeLowPass(sampleRate_, currentLowPass_.frequencyHz, currentLowPass_.q);
    highPassMix_ = currentHighPass_.enabled ? 1.0f : 0.0f;
    lowPassMix_ = currentLowPass_.enabled ? 1.0f : 0.0f;
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
  highPassStates_ = {};
  lowPassStates_ = {};
  scalarSamplesUntilUpdate_ = 0;
}

} // namespace ardor
