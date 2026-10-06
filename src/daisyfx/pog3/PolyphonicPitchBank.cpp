#include "daisyfx/pog3/PolyphonicPitchBank.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ardor::pog3 {
namespace {
constexpr double kPi = 3.14159265358979323846, kTwoPi = 2 * kPi;
double principal(double phase) noexcept { return std::remainder(phase, kTwoPi); }
double sinc(double x) noexcept { return std::fabs(x) < 1e-12 ? 1 : std::sin(kPi * x) / (kPi * x); }
float highWeight(float frequencyHz) noexcept {
  // The low-band analysis resolves ordinary low chord fundamentals that merge
  // in the short/long main windows. Both resolutions use the same crossover.
  const float t = std::clamp((frequencyHz - 200.0f) / 100.0f, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
}

PitchInterpolation::PitchInterpolation() {
  // Centering the FFT removes its alternating-bin phase. The DTFT is then
  // interpolated by a windowed sinc, rather than interpolating opposing complex
  // coefficients directly. Lanczos support is fixed and prepared once.
  for (std::size_t p = 0; p <= kPhases; ++p) {
    const double fraction = static_cast<double>(p) / kPhases;
    double sum = 0;
    for (int i = 1 - kRadius; i <= kRadius; ++i) {
      const double distance = fraction - i;
      const double weight = sinc(distance) * sinc(distance / kRadius);
      values_[p][i + kRadius - 1] = static_cast<float>(weight);
      sum += weight;
    }
    for (auto& weight : values_[p]) weight /= static_cast<float>(sum);
  }
}

PitchPlan::PitchPlan(std::shared_ptr<const SpectralPlan> spectralPlan,
                     std::shared_ptr<const PitchInterpolation> interpolation)
    : spectral(std::move(spectralPlan)), interpolation_(std::move(interpolation)) {
  if (!spectral) throw std::invalid_argument("pitch plan requires a spectral plan");
  if (!interpolation_) interpolation_ = std::make_shared<PitchInterpolation>();
  const double n = spectral->frameSize();
  const auto dirichlet = [n](double x) {
    return std::fabs(x) < 1e-12 ? n : std::sin(kPi * x) * std::cos(kPi * x / n) / std::sin(kPi * x / n);
  };
  for (std::size_t i = 0; i < hannLobe_.size(); ++i) {
    const double x = static_cast<double>(i) / kPhases;
    hannLobe_[i] = static_cast<float>((.5 * dirichlet(x) + .25 * dirichlet(x - 1) + .25 * dirichlet(x + 1)) / n);
  }
}

float PitchPlan::lobe(float distance) const noexcept {
  if (!std::isfinite(distance)) return 0;
  const float position = std::fabs(distance) * kPhases;
  if (position >= hannLobe_.size() - 1) return 0;
  const auto i = static_cast<std::size_t>(position);
  return hannLobe_[i] + (position - i) * (hannLobe_[i + 1] - hannLobe_[i]);
}

void PitchFrame::prepare(std::shared_ptr<const PitchPlan> plan, float frequencyCeiling) {
  if (!plan || !std::isfinite(frequencyCeiling) || frequencyCeiling <= 0 || frequencyCeiling > kSampleRate / 2)
    throw std::invalid_argument("pitch frame requires a plan and a valid frequency ceiling");
  plan_ = std::move(plan);
  const auto n = plan_->spectral->frameSize();
  peakLimit_ = std::min(n / 2, static_cast<std::size_t>(std::ceil(frequencyCeiling * n / kSampleRate)));
  const auto bins = std::min(n / 2 + 1, peakLimit_ + 2);
  phase_.resize(bins);
  magnitude_.resize(bins);
  previousMagnitude_.resize(bins);
  candidates_.resize(bins);
  reset();
}

void PitchFrame::reset() noexcept {
  std::fill(phase_.begin(), phase_.end(), 0);
  std::fill(magnitude_.begin(), magnitude_.end(), 0);
  std::fill(previousMagnitude_.begin(), previousMagnitude_.end(), 0);
  tracks_ = {};
  regions_ = {};
  spectrum_ = {};
  count_ = capacityEvents_ = generation_ = 0;
  previous_ = false;
  centerSamples_ = samples_ = 0;
}

void PitchFrame::update(std::span<const std::complex<float>> spectrum) noexcept {
  count_ = 0;
  spectrum_ = {};
  if (!plan_ || spectrum.size() != plan_->spectral->frameSize()) return;
  samples_ += static_cast<std::int64_t>(plan_->spectral->hopSize());
  centerSamples_ = samples_ - static_cast<std::int64_t>(spectrum.size() / 2);
  spectrum_ = spectrum;
  const auto half = spectrum.size() / 2;
  float maximum = 0;
  for (std::size_t k = 0; k < magnitude_.size(); ++k) {
    magnitude_[k] = std::abs(spectrum[k]);
    if (!std::isfinite(magnitude_[k])) { spectrum_ = {}; previous_ = false; return; }
    maximum = std::max(maximum, magnitude_[k]);
  }
  std::size_t candidates = 0;
  const double step = kTwoPi * plan_->spectral->hopSize() / spectrum.size();
  for (std::size_t k = 0; k <= peakLimit_; ++k) {
    if (magnitude_[k] <= std::max(1e-7f, maximum * .00001f)
        || (k && magnitude_[k] <= magnitude_[k - 1])
        || (k < half && magnitude_[k] < magnitude_[k + 1])) continue;
    float frequency;
    if (k == 0 || k == half) frequency = static_cast<float>(k);
    else if (previous_ && previousMagnitude_[k] > 1e-7f) {
      const double delta = principal(std::arg(spectrum[k]) - phase_[k] - k * step);
      frequency = static_cast<float>(k + delta / step);
    } else {
      const double a = std::log(std::max(magnitude_[k - 1], 1e-20f));
      const double b = std::log(std::max(magnitude_[k], 1e-20f));
      const double c = std::log(std::max(magnitude_[k + 1], 1e-20f));
      const double denominator = a - 2 * b + c;
      frequency = k + static_cast<float>(denominator == 0 ? 0 : std::clamp(.5 * (a - c) / denominator, -.5, .5));
    }
    candidates_[candidates++] = {static_cast<std::uint16_t>(k), 0, 0, 0, 0,
      std::clamp(frequency, 0.0f, static_cast<float>(half)), magnitude_[k]};
  }
  if (candidates > regions_.size()) {
    ++capacityEvents_;
    std::sort(candidates_.begin(), candidates_.begin() + candidates, [](const auto& a, const auto& b) {
      return a.magnitude == b.magnitude ? a.bin < b.bin : a.magnitude > b.magnitude;
    });
    candidates = regions_.size();
  }
  std::sort(candidates_.begin(), candidates_.begin() + candidates,
            [](const auto& a, const auto& b) { return a.bin < b.bin; });
  std::array<bool, kMaxPitchPartials> used{};
  for (auto& track : tracks_) if (track.generation) track.missed = std::min(track.missed + 1, 5U);
  struct Prediction { float frequency; std::size_t track; };
  std::array<Prediction, kMaxPitchPartials> predictions{};
  std::size_t predicted = 0;
  for (std::size_t t = 0; t < tracks_.size(); ++t)
    if (tracks_[t].generation && tracks_[t].missed <= 4)
      predictions[predicted++] = {tracks_[t].frequency + tracks_[t].velocity, t};
  std::sort(predictions.begin(), predictions.begin() + predicted, [](const auto& a, const auto& b) {
    return a.frequency == b.frequency ? a.track < b.track : a.frequency < b.frequency;
  });
  for (std::size_t p = 0; p < candidates; ++p) {
    auto region = candidates_[p];
    std::size_t match = tracks_.size();
    float distance = 2.0f;
    auto prediction = std::lower_bound(predictions.begin(), predictions.begin() + predicted,
      region.frequencyBins - 2, [](const auto& value, float frequency) { return value.frequency < frequency; });
    for (; prediction != predictions.begin() + predicted && prediction->frequency < region.frequencyBins + 2; ++prediction) {
      if (used[prediction->track]) continue;
      const float d = std::fabs(region.frequencyBins - prediction->frequency);
      if (d < distance || (d == distance && prediction->track < match)) { distance = d; match = prediction->track; }
    }
    bool birth = match == tracks_.size();
    if (birth) {
      for (std::size_t t = 0; t < tracks_.size(); ++t)
        if (!used[t] && (!tracks_[t].generation || tracks_[t].missed > 4)) { match = t; break; }
      if (match == tracks_.size()) {
        unsigned oldest = 0;
        for (std::size_t t = 0; t < tracks_.size(); ++t)
          if (!used[t] && tracks_[t].missed > oldest) { oldest = tracks_[t].missed; match = t; }
      }
    }
    if (match == tracks_.size()) { ++capacityEvents_; continue; }
    auto& track = tracks_[match];
    if (birth) { track = {}; track.generation = ++generation_; }
    track.velocity = birth ? 0 : std::clamp(region.frequencyBins - track.frequency, -.5f, .5f);
    track.frequency = region.frequencyBins;
    track.missed = 0;
    used[match] = true;
    region.track = static_cast<std::uint16_t>(match);
    region.generation = track.generation;
    regions_[count_++] = region;
  }
  // Partition every positive bin: pick noise and transients retain their complex
  // representation and phase relative to their nearest prominent partial.
  for (std::size_t p = 0; p < count_; ++p) {
    regions_[p].first = p == 0 ? 0 : (regions_[p - 1].bin + regions_[p].bin) / 2 + 1;
    regions_[p].last = p + 1 == count_ ? half : (regions_[p].bin + regions_[p + 1].bin) / 2;
  }
  for (std::size_t k = 0; k < magnitude_.size(); ++k) { phase_[k] = std::arg(spectrum[k]); previousMagnitude_[k] = magnitude_[k]; }
  previous_ = true;
}

void PitchRenderer::prepare(std::shared_ptr<const PitchPlan> plan, bool allowHeld) {
  if (!plan) throw std::invalid_argument("pitch renderer requires a plan");
  plan_ = std::move(plan);
  synthesis_.prepare(plan_->spectral, true, plan_->spectral->hopSize());
  spectrum_.resize(plan_->spectral->frameSize());
  if (allowHeld) heldPhases_.resize(2 * kMaxPitchPartials);
  reset();
}

void PitchRenderer::reset() noexcept {
  synthesis_.reset();
  std::fill(spectrum_.begin(), spectrum_.end(), std::complex<float>{});
  phases_ = {};
  lowPhases_ = {};
  phaseAges_ = {};
  lowCarriers_ = {};
  std::fill(heldPhases_.begin(), heldPhases_.end(), HeldPhase{});
  shifted_ = false;
}

bool PitchRenderer::render(const PitchFrame& frame, float semitones, const PitchFrame* lowAnalysis,
                           std::size_t startOffset, const PartialGains* gains, const PartialGains* lowGains,
                           bool partialProcessing, const FrozenBand* held, const FrozenBand* heldLow,
                           float heldMix, float heldGain, const PitchRenderer* heldReference) noexcept {
  if (!plan_ || frame.spectrum().size() != spectrum_.size() || !std::isfinite(semitones)) return false;
  const float ratio = std::exp2(std::clamp(semitones, -24.0f, 24.0f) / 12);
  if (ratio != 1) shifted_ = true;
  // Outside partial processing, fresh unison retains the complete spectrum.
  // Returning an already shifted path to unison preserves its phase histories
  // and representation, avoiding a hard change during a Warp performance.
  if (!shifted_ && !partialProcessing) lowAnalysis = nullptr;
  const auto input = frame.spectrum();
  const int half = static_cast<int>(spectrum_.size() / 2);
  const double hopPhase = kTwoPi * plan_->spectral->hopSize() / spectrum_.size();
  std::fill(spectrum_.begin(), spectrum_.end(), std::complex<float>{});
  lowCarriers_ = {};
  if (lowAnalysis && !lowAnalysis->spectrum().empty()) {
    const auto lowInput = lowAnalysis->spectrum();
    const double scale = static_cast<double>(spectrum_.size()) / lowInput.size();
    for (const auto& region : lowAnalysis->regions()) {
      const float frequency = static_cast<float>(region.frequencyBins * scale);
      if (frequency * kSampleRate / spectrum_.size() >= 400.0f) continue;
      auto& phase = lowPhases_[region.track];
      auto& age = phaseAges_[kMaxPitchPartials + region.track];
      if (phase.generation != region.generation) {
        // Seed from the actual analyzed carrier. Multiplying its frequency by
        // absolute elapsed time would make a late Warp onset jump arbitrarily.
        phase = {.generation = region.generation, .frequency = frequency, .ratio = ratio}; age = 0;
      } else {
        phase.offset = principal(phase.offset + .5 * hopPhase
          * ((phase.ratio - 1) * phase.frequency + (ratio - 1) * frequency));
        phase.frequency = frequency;
        phase.ratio = ratio;
      }
      age = std::min<unsigned>(age + 1, 2);
      const float windowGain = lowAnalysis->lobe(static_cast<float>(region.bin) - region.frequencyBins)
        * lowInput.size();
      // Incoherent/noisy phase estimates can move outside the peak's main
      // lobe. Never divide by a near-zero sidelobe and amplify that noise.
      if (windowGain < .25f * lowInput.size()) continue;
      auto amplitude = lowInput[region.bin] / windowGain;
      if (region.bin & 1) amplitude = -amplitude;
      if (region.bin == 0) amplitude *= .5f;
      const double extrapolation = kTwoPi * region.frequencyBins
        * (frame.centerSamples() - lowAnalysis->centerSamples()) / lowInput.size();
      const double angle = principal(phase.offset + extrapolation);
      lowCarriers_[region.track] = amplitude
        * std::complex<float>{static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))}
        * std::min(age * .5f, 1.0f);
    }
  }
  for (const auto& region : frame.regions()) {
    auto& phase = phases_[region.track];
    auto& age = phaseAges_[region.track];
    if (phase.generation != region.generation) {
      phase = {.generation = region.generation, .frequency = region.frequencyBins, .ratio = ratio}; age = 0;
    } else {
      phase.offset = principal(phase.offset + .5 * hopPhase
        * ((phase.ratio - 1) * phase.frequency + (ratio - 1) * region.frequencyBins));
      phase.frequency = region.frequencyBins;
      phase.ratio = ratio;
    }
    age = std::min<unsigned>(age + 1, 2);
    const float shift = (ratio - 1) * region.frequencyBins;
    const float destination = ratio * region.frequencyBins;
    if (ratio != 1 && destination >= half) continue; // Drop the entire aliased region.
    // Taper the whole lobe before its support reaches the upper edge.
    const float edge = std::clamp((half - destination - 2) / 6, 0.0f, 1.0f);
    const float fade = ratio == 1 ? 1 : std::min(age * .5f, 1.0f);
    const float high = lowAnalysis ? highWeight(region.frequencyBins * kSampleRate / spectrum_.size()) : 1;
    const float gain = high * fade * (ratio == 1 ? 1 : edge * edge * (3 - 2 * edge))
      * (gains ? (*gains)[region.track] : 1);
    if (gain == 0) continue;
    double rotationPhase = phase.offset;
    if (lowAnalysis && region.frequencyBins * kSampleRate / spectrum_.size() < 400.0f) {
      const float hz = region.frequencyBins * kSampleRate / spectrum_.size();
      const float t = std::clamp((hz - 300.0f) / 100.0f, 0.0f, 1.0f);
      const float alignment = 1 - t * t * (3 - 2 * t);
      const PitchRegion* match = nullptr;
      float distance = 23.4375f;
      for (const auto& lowRegion : lowAnalysis->regions()) {
        const float d = std::fabs(hz - lowRegion.frequencyBins * kSampleRate / lowAnalysis->frameSize());
        if (d < distance && std::abs(lowCarriers_[lowRegion.track]) > 1e-10f) { match = &lowRegion; distance = d; }
      }
      if (match && alignment > 0) {
        const auto centered = (region.bin & 1) ? -input[region.bin] : input[region.bin];
        const double correction = std::arg(lowCarriers_[match->track]) - std::arg(centered) - phase.offset;
        phase.alignment += principal(correction - phase.alignment);
        rotationPhase += alignment * phase.alignment;
      }
    }
    const std::complex<float> rotation{static_cast<float>(std::cos(rotationPhase)) * gain,
                                       static_cast<float>(std::sin(rotationPhase)) * gain};
    const int shiftCeil = static_cast<int>(std::ceil(shift));
    const float position = (shiftCeil - shift) * PitchPlan::kPhases;
    const auto table = std::min(static_cast<std::size_t>(position), PitchPlan::kPhases - 1);
    const float fraction = position - table;
    PitchPlan::Weights weights{};
    if (position != 0) for (int i = 1 - PitchPlan::kRadius; i <= PitchPlan::kRadius; ++i) {
      const auto tap = i + PitchPlan::kRadius - 1;
      const float weight = plan_->interpolationWeights(table)[tap]
        + fraction * (plan_->interpolationWeights(table + 1)[tap] - plan_->interpolationWeights(table)[tap]);
      weights[tap] = ((shiftCeil - i) & 1) ? -weight : weight;
    }
    const auto accumulate = [&](int j, std::complex<float> value) {
      if (j <= -half || j > half) return;
      // Reflect the negative support of a positive analytic lobe; never wrap.
      if (j < 0) spectrum_[-j] += std::conj(value);
      else if (j == 0 || j == half) spectrum_[j] += std::complex<float>{2 * value.real(), 0};
      else spectrum_[j] += value;
    };
    // Scatter each owned source coefficient once. This is algebraically the
    // same centered sinc interpolation as gathering each destination bin, but
    // avoids scanning the whole kernel halo again for every adjacent region.
    for (int k = static_cast<int>(region.first); k <= static_cast<int>(region.last); ++k) {
      const float endpoint = k == 0 || k == half ? .5f : 1;
      const auto value = input[k] * rotation * endpoint;
      if (position == 0) {
        accumulate(k + shiftCeil, (shiftCeil & 1) ? -value : value);
      } else {
        for (int i = 1 - PitchPlan::kRadius; i <= PitchPlan::kRadius; ++i)
          accumulate(k + shiftCeil - i, value * weights[i + PitchPlan::kRadius - 1]);
      }
    }
  }
  if (lowAnalysis && !lowAnalysis->spectrum().empty()) {
    const auto lowInput = lowAnalysis->spectrum();
    const double scale = static_cast<double>(spectrum_.size()) / lowInput.size();
    for (const auto& region : lowAnalysis->regions()) {
      const float frequency = static_cast<float>(region.frequencyBins * scale);
      const float low = 1 - highWeight(frequency * kSampleRate / spectrum_.size());
      if (low == 0) continue;
      auto amplitude = lowCarriers_[region.track] * low * (lowGains ? (*lowGains)[region.track] : 1);
      const float destination = frequency * ratio;
      const int first = static_cast<int>(std::floor(destination)) - 16;
      const int last = static_cast<int>(std::ceil(destination)) + 16;
      for (int j = first; j <= last; ++j) {
        if (std::abs(j) >= half) continue;
        auto value = amplitude * (plan_->lobe(j - destination) * spectrum_.size());
        if (j & 1) value = -value;
        if (j < 0) spectrum_[-j] += std::conj(value);
        else if (j == 0) spectrum_[0] += std::complex<float>{2 * value.real(), 0};
        else spectrum_[j] += value;
      }
    }
  }
  if (held && heldMix > 0 && !heldPhases_.empty()) {
    for (int j = 0; j <= half; ++j) spectrum_[j] *= 1 - heldMix;
    renderHeld(*held, false, ratio, heldMix * heldGain, frame.centerSamples(), heldReference);
    if (heldLow) renderHeld(*heldLow, true, ratio, heldMix * heldGain, frame.centerSamples(), heldReference);
  }
  for (int j = 1; j < half; ++j) spectrum_[spectrum_.size() - j] = std::conj(spectrum_[j]);
  return synthesis_.addFrameInPlace(spectrum_, startOffset);
}

void PitchRenderer::renderHeld(const FrozenBand& band, bool low, float ratio, float gain, std::int64_t center,
                              const PitchRenderer* reference) noexcept {
  const int half = static_cast<int>(spectrum_.size() / 2);
  for (std::size_t i = 0; i < band.count; ++i) {
    const auto& p = band.partials[i];
    if (!p.id) continue;
    auto& phase = heldPhases_[(low ? kMaxPitchPartials : 0) + i];
    const float outputHz = p.frequency * ratio;
    if (phase.id != p.id) {
      double offset = 0;
      const auto* seed = p.referenceLeft && reference ? reference : this;
      const auto& live = low ? seed->lowPhases_[p.liveTrack] : seed->phases_[p.liveTrack];
      if (live.generation == p.liveGeneration) offset = live.offset;
      phase = {p.id, principal(p.phase + kTwoPi * p.frequency * (center - p.center) / kSampleRate + offset), center, outputHz};
    } else {
      phase.phase = principal(phase.phase + .5 * kTwoPi * (phase.frequency + outputHz) * (center - phase.center) / kSampleRate);
      phase.center = center; phase.frequency = outputHz;
    }
    if (outputHz >= kSampleRate / 2) continue;
    const float destination = outputHz * spectrum_.size() / kSampleRate;
    const float edge = std::clamp((half - destination - 2) / 6, 0.0f, 1.0f);
    const float weight = low ? 1 - highWeight(p.frequency) : highWeight(p.frequency);
    const float amplitude = p.magnitude * weight * gain * (ratio == 1 ? 1 : edge * edge * (3 - 2 * edge));
    const std::complex<float> carrier{static_cast<float>(std::cos(phase.phase)) * amplitude,
                                      static_cast<float>(std::sin(phase.phase)) * amplitude};
    const int first = static_cast<int>(std::floor(destination)) - 16;
    const int last = static_cast<int>(std::ceil(destination)) + 16;
    for (int j = first; j <= last; ++j) {
      if (std::abs(j) >= half) continue;
      auto value = carrier * (plan_->lobe(j - destination) * spectrum_.size());
      if (j & 1) value = -value;
      if (j < 0) spectrum_[-j] += std::conj(value);
      else if (j == 0) spectrum_[0] += std::complex<float>{2 * value.real(), 0};
      else spectrum_[j] += value;
    }
  }
}

void PolyphonicPitchBank::prepare() {
  auto interpolation = std::make_shared<PitchInterpolation>();
  auto longPlan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(2048, 256), interpolation);
  auto shortPlan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(1024, 128), interpolation);
  auto lowPlan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(4096, 512), interpolation);
  for (std::size_t channel = 0; channel < 2; ++channel) {
    longAnalysis_[channel].prepare(longPlan->spectral);
    shortAnalysis_[channel].prepare(shortPlan->spectral);
    longFrames_[channel].prepare(longPlan);
    shortFrames_[channel].prepare(shortPlan);
    lowAnalysis_[channel].prepare(lowPlan->spectral);
    lowFrames_[channel].prepare(lowPlan, 400);
    for (auto& voice : longVoices_) voice[channel].prepare(longPlan, true);
    for (auto& voice : shortVoices_) voice[channel].prepare(shortPlan);
  }
  freeze_ = std::make_unique<SpectralFreeze>();
  freeze_->setControls(freezeMode_, freezePosition_, freezeDry_);
  prepared_ = true;
  reset();
}

void PolyphonicPitchBank::reset() noexcept {
  for (auto& analysis : longAnalysis_) analysis.reset();
  for (auto& analysis : shortAnalysis_) analysis.reset();
  for (auto& analysis : lowAnalysis_) analysis.reset();
  for (auto& frame : longFrames_) frame.reset();
  for (auto& frame : shortFrames_) frame.reset();
  for (auto& frame : lowFrames_) frame.reset();
  for (auto& voice : longVoices_) for (auto& channel : voice) channel.reset();
  for (auto& voice : shortVoices_) for (auto& channel : voice) channel.reset();
  warp_ = warpTarget_;
  focus_ = focusTarget_ ? 1 : 0;
  transforms_ = 0;
  deadlineMisses_ = 0;
  longJob_ = 12; shortJob_ = 4;
  longAge_ = shortAge_ = 0;
  frameWarp_ = warp_;
  attack_.reset();
  if (freeze_) freeze_->reset();
  freezeReady_ = true;
  longAttackReady_ = shortAttackReady_ = true;
  longAttackActive_ = attack_.seconds() > 0;
  inputSamples_ = 0;
  for (auto* pair : {&longGains_, &shortGains_, &lowGains_}) for (auto& channel : *pair) channel.fill(1);
  healthy_ = true;
}

bool PolyphonicPitchBank::setWarp(float normalized) noexcept {
  if (!std::isfinite(normalized)) return false;
  warpTarget_ = std::clamp(normalized, 0.0f, 1.0f);
  return true;
}
bool PolyphonicPitchBank::setFreeze(ExpressionMode mode, float position, bool dryEligible) noexcept {
  if (!std::isfinite(position) || static_cast<int>(mode) < 0 || static_cast<int>(mode) > 6) return false;
  freezeMode_ = mode; freezePosition_ = std::clamp(position, 0.0f, 1.0f); freezeDry_ = dryEligible;
  return !freeze_ || freeze_->setControls(mode, freezePosition_, dryEligible);
}

PitchVoices PolyphonicPitchBank::process(PitchStereo input) noexcept {
  PitchVoices result{};
  if (!prepared_) return result;
  ++inputSamples_;
  constexpr float step = 1.0f / 1200; // Reversible 25 ms joint upper-pair fade.
  focus_ = focusTarget_ ? std::min(1.0f, focus_ + step) : std::max(0.0f, focus_ - step);
  warp_ += .0020811647f * (warpTarget_ - warp_); // 10 ms, sample-rate based.
  if (std::fabs(warpTarget_ - warp_) < 1e-6f) warp_ = warpTarget_;
  const std::array<float, 2> source{input.left, input.right};
  if (longJob_ < 12) ++longAge_;
  if (shortJob_ < 4) ++shortAge_;
  bool newLong = false, newShort = false, newLow = false;
  for (std::size_t channel = 0; channel < 2; ++channel) {
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
      float value = longVoices_[voice][channel].pop();
      if (voice >= 4) value = focus_ * value + (1 - focus_) * shortVoices_[voice - 4][channel].pop();
      (channel ? result[voice].right : result[voice].left) = value;
    }
    if (longAnalysis_[channel].push(source[channel])) {
      longFrames_[channel].update(longAnalysis_[channel].spectrum());
      ++transforms_;
      newLong = true;
    }
    if (shortAnalysis_[channel].push(source[channel])) {
      shortFrames_[channel].update(shortAnalysis_[channel].spectrum());
      ++transforms_;
      newShort = true;
    }
    if (lowAnalysis_[channel].push(source[channel])) {
      lowFrames_[channel].update(lowAnalysis_[channel].spectrum());
      ++transforms_;
      newLow = true;
    }
  }
  // The small low-band model is ready at the frame boundary; long and short
  // stereo interpretation are separate staged jobs before their render jobs.
  // No gain array changes while jobs using that frame are still outstanding.
  if (newLow) lowGains_ = attack_.update(lowFrames_[0], lowFrames_[1], 2, inputSamples_);
  if (newLong) {
    if (longJob_ != 12) { ++deadlineMisses_; healthy_ = false; }
    longJob_ = longAge_ = 0;
    longAttackReady_ = false;
    freezeReady_ = false;
    frameWarp_ = warp_; // Every renderer uses controls at this frame's timestamp.
  }
  if (newShort) {
    if (shortJob_ != 4) { ++deadlineMisses_; healthy_ = false; }
    shortJob_ = shortAge_ = 0;
    shortAttackReady_ = false;
  }
  if (!longAttackReady_ && longAge_ >= 1) {
    longGains_ = attack_.update(longFrames_[0], longFrames_[1], 0, inputSamples_ - longAge_);
    longAttackActive_ = attack_.seconds() > 0;
    longAttackReady_ = true;
  }
  if (!shortAttackReady_ && shortAge_ >= 8) {
    shortGains_ = attack_.update(shortFrames_[0], shortFrames_[1], 1, inputSamples_ - shortAge_);
    shortAttackReady_ = true;
  }
  if (!freezeReady_ && longAttackReady_ && shortAttackReady_ && longAge_ >= 9) {
    freeze_->update(longFrames_, shortFrames_, lowFrames_, longGains_, shortGains_, lowGains_, inputSamples_ - longAge_);
    freezeReady_ = true;
  }
  // Immutable until their next analysis event: each long frame's jobs complete
  // by sample 251 of its 256-sample hop, each short frame's by sample 112 of 128.
  // Both sets of jobs finish before the 512-hop low-band frame can change.
  // The explicit H staging makes every window start at t+1+H,
  // independently of the sample on which its bounded render job executes.
  while (longAttackReady_ && longJob_ < 12 && 17 + longJob_ * 256 / 12 <= longAge_) {
    if (longAge_ >= 256) { ++deadlineMisses_; healthy_ = false; break; }
    const auto voice = longJob_ / 2, channel = longJob_ % 2;
    if (!longVoices_[voice][channel].render(longFrames_[channel], kVoiceSemitones[voice] * frameWarp_, &lowFrames_[channel],
                                           256 - longAge_, &longGains_[channel], &lowGains_[channel], longAttackActive_,
                                           &freeze_->band(0, channel), &freeze_->band(2, channel), freeze_->mix(voice), freeze_->gain(),
                                           channel ? &longVoices_[voice][0] : nullptr)) healthy_ = false;
    ++transforms_; ++longJob_;
  }
  while (shortAttackReady_ && shortJob_ < 4 && 16 + shortJob_ * 128 / 4 <= shortAge_) {
    if (shortAge_ >= 128) { ++deadlineMisses_; healthy_ = false; break; }
    const auto voice = shortJob_ / 2, channel = shortJob_ % 2;
    if (!shortVoices_[voice][channel].render(shortFrames_[channel], kVoiceSemitones[voice + 4], &lowFrames_[channel],
                                            128 - shortAge_, &shortGains_[channel], &lowGains_[channel])) healthy_ = false;
    ++transforms_; ++shortJob_;
  }
  return result;
}

std::size_t PolyphonicPitchBank::capacityEvents() const noexcept {
  std::size_t result = 0;
  for (const auto& frame : longFrames_) result += frame.capacityEvents();
  for (const auto& frame : shortFrames_) result += frame.capacityEvents();
  for (const auto& frame : lowFrames_) result += frame.capacityEvents();
  return result;
}
} // namespace ardor::pog3
