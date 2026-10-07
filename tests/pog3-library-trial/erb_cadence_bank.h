#pragma once

#include "erb_shared_bank.h"
#include <type_traits>

namespace pog3_trial {

inline double erbWrapped(double phase) noexcept {
  constexpr double tau = 2 * std::numbers::pi;
  phase -= tau * static_cast<int>(phase / tau);
  if (phase > std::numbers::pi) phase -= tau;
  if (phase < -std::numbers::pi) phase += tau;
  return phase;
}

// Taylor cosine through degree 18 on [-pi, pi]. The recurrence coefficient
// needs more accuracy than a linearly interpolated cosine: its error otherwise
// grows quadratically over a short block near DC/Nyquist.
inline double erbRecurrenceCos(double phase) noexcept {
  const double x = phase * phase;
  return 1 + x * (-1.0 / 2 + x * (1.0 / 24 + x * (-1.0 / 720 + x * (1.0 / 40320
    + x * (-1.0 / 3628800 + x * (1.0 / 479001600 + x * (-1.0 / 87178291200
    + x * (1.0 / 20922789888000 + x * (-1.0 / 6402373705728000)))))))));
}

// Delayed endpoint interpolation. CountCycles preserves the full-rate input
// winding count; the cheap alternative infers it from the nominal band center
// and therefore aliases off-band carriers. Both are experimental CPU controls.
template<std::size_t Stride, bool CountCycles>
class ErbCadenceVoices {
public:
  static_assert(Stride >= 2 && Stride <= 32 && (Stride & (Stride - 1)) == 0);
  using Ratios = ErbPhaseVoices::Ratios;
  using Samples = std::array<float, 8>;

  void prepare(double center) noexcept {
    centerAdvance_ = 2 * std::numbers::pi * center / 48000 * Stride;
    tables_ = &erbPhaseTables();
  }
  void reset() noexcept {
    phase_ = {}; accumulated_ = {}; real_ = {}; previousReal_ = {}; coefficient_ = {};
    segmentAngle_ = endpointAngle_ = amplitude_ = amplitudeStep_ = endpointAmplitude_ = 0;
    previousCarrier_ = {}; turns_ = 0;
    angleValid_ = phaseValid_ = endpointValid_ = ready_ = false; position_ = 0; interval_ = 0;
  }

  // Flush the input phase/count accumulated under the previous control value.
  // Controls need not coincide with interpolation endpoints.
  void controlBoundary(const Ratios& previousRatios) noexcept {
    if (CountCycles ? phaseValid_ : endpointValid_) {
      double advance = interval_;
      if constexpr (CountCycles) {
        const double angle = tables_->angle(previousCarrier_);
        advance = angle - segmentAngle_ + 2 * std::numbers::pi * turns_;
        segmentAngle_ = angle; turns_ = 0;
      }
      for (std::size_t v = 0; v < 8; ++v) accumulated_[v] += previousRatios[v] * advance;
    }
    interval_ = 0;
  }

  [[gnu::always_inline]] Samples process(std::complex<double> carrier, const Ratios& ratios) noexcept {
    const double power = std::norm(carrier);
    if constexpr (CountCycles) {
      if (power >= 1e-40) {
        if (!phaseValid_) acquirePhase(carrier, ratios);
        else if (angleValid_) {
          // A sign crossing at the real axis wraps the principal angle only
          // when its direction (cross product) takes the negative-real route.
          // Unlike testing just the new real part, this works for large
          // principal increments close to Nyquist too.
          if (previousCarrier_.imag() >= 0 && carrier.imag() < 0) {
            const double cross = previousCarrier_.real() * carrier.imag() - previousCarrier_.imag() * carrier.real();
            if (cross > 0) ++turns_;
          } else if (previousCarrier_.imag() < 0 && carrier.imag() >= 0) {
            const double cross = previousCarrier_.real() * carrier.imag() - previousCarrier_.imag() * carrier.real();
            if (cross < 0) --turns_;
          }
        }
        previousCarrier_ = carrier; angleValid_ = true;
      } else angleValid_ = phaseValid_ = false;
    } else if (endpointValid_) {
      interval_ += 1;
    }

    if (position_ == 0) updateEndpoint(carrier, power, ratios);
    position_ = (position_ + 1) & (Stride - 1);
    Samples result{};
    if (ready_) {
      for (std::size_t v = 0; v < 8; ++v) {
        result[v] = amplitude_ * real_[v];
        const auto nextReal = coefficient_[v] * real_[v] - previousReal_[v];
        previousReal_[v] = real_[v];
        real_[v] = nextReal;
      }
      amplitude_ += amplitudeStep_;
    }
    return result;
  }

private:
  [[gnu::noinline]] void acquirePhase(std::complex<double> carrier, const Ratios& ratios) noexcept {
    // Preserve the original full-rate phase origin even when the first
    // meaningful carrier arrives between interpolation endpoints.
    segmentAngle_ = tables_->angle(carrier);
    for (std::size_t v = 0; v < 8; ++v) phase_[v] = erbWrapped(ratios[v] * segmentAngle_);
    accumulated_ = {}; turns_ = 0; phaseValid_ = true;
    endpointValid_ = ready_ = false;
  }

  [[gnu::noinline]] void updateEndpoint(std::complex<double> carrier, double power, const Ratios& ratios) noexcept {
    double angle = 0;
    if (power < 1e-40) {
      endpointValid_ = ready_ = false;
      amplitude_ = endpointAmplitude_ = 0; accumulated_ = {}; interval_ = 0; turns_ = 0;
    } else {
      angle = tables_->angle(carrier);
      const double magnitude = std::sqrt(power);
      if (!endpointValid_) {
        if constexpr (CountCycles) {
          const double advance = angle - segmentAngle_ + 2 * std::numbers::pi * turns_;
          for (std::size_t v = 0; v < 8; ++v) phase_[v] = erbWrapped(phase_[v] + accumulated_[v] + ratios[v] * advance);
        } else for (std::size_t v = 0; v < 8; ++v) phase_[v] = erbWrapped(ratios[v] * angle);
        endpointValid_ = true; ready_ = false;
      } else {
        double inputAdvance;
        if constexpr (CountCycles)
          inputAdvance = angle - segmentAngle_ + 2 * std::numbers::pi * turns_;
        else
          inputAdvance = centerAdvance_ + erbWrapped(angle - endpointAngle_ - centerAdvance_);
        for (std::size_t v = 0; v < 8; ++v) {
          const double integrated = accumulated_[v] + ratios[v] * (CountCycles ? inputAdvance : interval_);
          const double advance = CountCycles ? integrated : inputAdvance * integrated / Stride;
          // Every block reanchors its oscillator to the double endpoint
          // history; float recurrence drift cannot accumulate across blocks.
          real_[v] = tables_->cos(phase_[v]);
          const double step = erbWrapped(advance / Stride);
          previousReal_[v] = tables_->cos(erbWrapped(phase_[v] - step));
          coefficient_[v] = 2 * erbRecurrenceCos(step);
          phase_[v] = erbWrapped(phase_[v] + advance);
        }
        amplitude_ = endpointAmplitude_;
        amplitudeStep_ = (magnitude - endpointAmplitude_) / Stride;
        ready_ = true;
      }
      endpointAmplitude_ = magnitude; endpointAngle_ = segmentAngle_ = angle;
      accumulated_ = {}; interval_ = 0; turns_ = 0;
    }
  }

  Ratios phase_{}, accumulated_{};
  // Longer blocks need double recurrence state to bound near-DC/Nyquist error.
  using Oscillators = std::array<std::conditional_t<(Stride >= 32), double, float>, 8>;
  Oscillators real_{}, previousReal_{}, coefficient_{};
  double centerAdvance_ = 0, segmentAngle_ = 0, endpointAngle_ = 0;
  double interval_ = 0;
  std::complex<double> previousCarrier_{};
  int turns_ = 0;
  float amplitude_ = 0, amplitudeStep_ = 0, endpointAmplitude_ = 0;
  std::size_t position_ = 0;
  bool angleValid_ = false, phaseValid_ = false, endpointValid_ = false, ready_ = false;
  const ErbPhaseTables* tables_ = nullptr;
};

template<std::size_t Stride, bool CountCycles>
class ErbCadenceBank {
public:
  static constexpr std::size_t bandCount = 69, voiceCount = 8;
  using Frame = ErbSharedBank<true>::Frame;
  ErbCadenceBank() {
    const ErbSharedBank<true> prepared;
    design_ = prepared.design();
    for (std::size_t k = 0; k < bandCount; ++k) {
      poles_[k] = {static_cast<float>(design_[k].pole.real()), static_cast<float>(design_[k].pole.imag())};
      numerators_[k] = design_[k].numerator;
      for (auto& channel : voices_) channel[k].prepare(design_[k].center);
    }
    setWarp(1);
  }
  void setWarp(double extent) noexcept {
    for (auto& channel : voices_) for (auto& band : channel) band.controlBoundary(ratios_);
    extent = std::clamp(extent, 0.0, 1.0);
    constexpr std::array<double, 8> semitones{0, -24, -12, 7, 12, 24, 12, 24};
    for (std::size_t v = 0; v < 8; ++v) {
      ratios_[v] = std::exp2(semitones[v] * (v >= 6 ? 1 : extent) / 12);
      for (std::size_t k = 0; k < bandCount; ++k) {
        const double edge = ratios_[v] * (design_[k].center + 4 * design_[k].bandwidth);
        const double t = std::clamp((edge - 18000) / 2000, 0.0, 1.0);
        gains_[k][v] = .5 + .5 * std::cos(std::numbers::pi * t);
      }
    }
  }
  void reset() noexcept {
    first_ = {}; second_ = {}; previous_ = {}; beforePrevious_ = {};
    for (auto& channel : voices_) for (auto& band : channel) band.reset();
  }
  Frame process(std::array<float, 2> input) noexcept {
    Frame result{};
    for (std::size_t c = 0; c < 2; ++c) {
      const float difference = input[c] - beforePrevious_[c];
      beforePrevious_[c] = previous_[c]; previous_[c] = input[c];
      for (std::size_t k = 0; k < bandCount; ++k) {
        first_[c][k] = poles_[k] * first_[c][k] + numerators_[k] * difference;
        second_[c][k] = poles_[k] * second_[c][k] + first_[c][k];
        const auto samples = voices_[c][k].process(std::complex<double>(second_[c][k]), ratios_);
        for (std::size_t v = 0; v < 8; ++v) result[v][c] += samples[v] * gains_[k][v];
      }
    }
    return result;
  }
private:
  std::array<ErbPs2Reference::Design, bandCount> design_{};
  std::array<std::complex<float>, bandCount> poles_{};
  std::array<float, bandCount> numerators_{};
  std::array<std::array<std::complex<float>, bandCount>, 2> first_{}, second_{};
  std::array<std::array<ErbCadenceVoices<Stride, CountCycles>, bandCount>, 2> voices_{};
  std::array<std::array<float, 8>, bandCount> gains_{};
  ErbPhaseVoices::Ratios ratios_{};
  std::array<float, 2> previous_{}, beforePrevious_{};
};
}
