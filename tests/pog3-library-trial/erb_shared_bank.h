#pragma once

#include "erb_ps2_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace pog3_trial {

struct ErbPhaseTables {
  static constexpr std::size_t atanSize = 1024, cosSize = 4096;
  std::array<double, atanSize + 1> atan{};
  std::array<float, cosSize + 1> cosine{};
  ErbPhaseTables() {
    for (std::size_t i = 0; i <= atanSize; ++i) atan[i] = std::atan(static_cast<double>(i) / atanSize);
    for (std::size_t i = 0; i <= cosSize; ++i) cosine[i] = std::cos(2 * std::numbers::pi * i / cosSize);
  }
  double angle(std::complex<double> z) const noexcept {
    const double x = std::fabs(z.real()), y = std::fabs(z.imag());
    const bool steep = y > x;
    const double t = steep ? x / y : y / x;
    const double position = t * atanSize;
    const auto index = std::min(static_cast<std::size_t>(position), atanSize - 1);
    double a = atan[index] + (atan[index + 1] - atan[index]) * (position - index);
    if (steep) a = std::numbers::pi / 2 - a;
    if (z.real() < 0) a = std::numbers::pi - a;
    return z.imag() < 0 ? -a : a;
  }
  double cos(double phase) const noexcept {
    // Caller supplies a phase reduced to [-pi, pi].
    const double position = std::fabs(phase) * (cosSize / (2 * std::numbers::pi));
    const auto index = static_cast<std::size_t>(position);
    return cosine[index] + (static_cast<double>(cosine[index + 1]) - cosine[index]) * (position - index);
  }
};

inline const ErbPhaseTables& erbPhaseTables() {
  static const ErbPhaseTables tables;
  return tables;
}

// Accurate-math and lookup CPU baselines for continuously moving ratios. Phases are
// integrated, not obtained by multiplying a growing input phase by a moving
// ratio. All histories are independent per band/channel/voice.
class ErbPhaseVoices {
public:
  static constexpr std::size_t voiceCount = 8;
  using Ratios = std::array<double, voiceCount>;
  using Samples = std::array<double, voiceCount>;

  template<bool Lookup = false>
  Samples process(std::complex<double> carrier, const Ratios& ratios, const ErbPhaseTables* tables = nullptr) noexcept {
    constexpr double tau = 2 * std::numbers::pi;
    const double magnitude = std::sqrt(std::norm(carrier));
    // A -400 dB carrier has no meaningful phase. Reacquire from its principal
    // phase after silence instead of propagating undefined phase increments.
    if (magnitude < 1e-20) { active_ = false; return {}; }
    double angle;
    if constexpr (Lookup) angle = tables->angle(carrier);
    else angle = std::atan2(carrier.imag(), carrier.real());
    if (!active_) {
      for (std::size_t v = 0; v < voiceCount; ++v) phase_[v] = ratios[v] * angle;
      active_ = true;
    } else {
      double delta = angle - previousAngle_;
      if (delta > std::numbers::pi) delta -= tau;
      if (delta < -std::numbers::pi) delta += tau;
      for (std::size_t v = 0; v < voiceCount; ++v) phase_[v] += ratios[v] * delta;
    }
    previousAngle_ = angle;
    for (std::size_t v = 0; v < voiceCount; ++v) {
      // Ratios stay in [.25, 4]. A principal increment is in [-pi, pi], so
      // two explicit reductions suffice and allow vectorization across voices.
      double p = phase_[v];
      p = p > std::numbers::pi ? p - tau : p;
      p = p < -std::numbers::pi ? p + tau : p;
      p = p > std::numbers::pi ? p - tau : p;
      p = p < -std::numbers::pi ? p + tau : p;
      phase_[v] = p;
    }
    Samples result{};
    for (std::size_t v = 0; v < voiceCount; ++v) {
      if constexpr (Lookup) result[v] = magnitude * tables->cos(phase_[v]);
      else result[v] = magnitude * std::cos(phase_[v]);
    }
    return result;
  }
  void reset() noexcept { previousAngle_ = 0; phase_ = {}; active_ = false; }

private:
  double previousAngle_ = 0;
  Samples phase_{};
  bool active_ = false;
};

// The 43-band variant retains the paper's limited grid as an analysis-sharing
// control. The 69-band variant extends that same spacing/BW rule to roughly
// 35 Hz--20 kHz source centers. Neither is a production POG3 backend.
template<bool Wide, bool Lookup = false>
class ErbSharedBank {
public:
  static constexpr std::size_t bandCount = Wide ? 69 : 43;
  static constexpr std::size_t voiceCount = ErbPhaseVoices::voiceCount;
  using Frame = std::array<std::array<float, 2>, voiceCount>;
  using Design = ErbPs2Reference::Design;

  ErbSharedBank() {
    if constexpr (Lookup) tables_ = &erbPhaseTables(); // Fully prepared off callback.
    if constexpr (!Wide) {
      const ErbPs2Reference reference;
      design_ = reference.design();
    } else {
      for (std::size_t k = 0; k < bandCount; ++k) {
        auto& d = design_[k];
        const double erb = 2.5 + k * (4.0 / 6);
        const double target = 228.7 * (std::pow(10.0, erb / 21.3) - 1);
        d.center = target / 2;
        d.bandwidth = (24.7 + .108 * target) / 12;
        const double q = d.center / d.bandwidth / 2;
        const double kBq = std::tan(std::numbers::pi * d.center / 48000);
        const double denominator = kBq * kBq * q + kBq + q;
        const double a1 = 2 * q * (kBq * kBq - 1) / denominator;
        const double a2 = (kBq * kBq * q - kBq + q) / denominator;
        const double real = -a1 / 2;
        d.pole = {real, std::sqrt(a2 - real * real)};
        const auto z = std::polar(1.0, -2 * std::numbers::pi * d.center / 48000);
        d.numerator = 2 * std::norm(1.0 - d.pole * z) / std::abs(1.0 - z * z);
      }
    }
    for (std::size_t k = 0; k < bandCount; ++k) {
      poles_[k] = {static_cast<float>(design_[k].pole.real()), static_cast<float>(design_[k].pole.imag())};
      numerators_[k] = static_cast<float>(design_[k].numerator);
    }
    setWarp(1);
  }

  void setWarp(double extent) noexcept {
    extent = std::clamp(extent, 0.0, 1.0);
    constexpr std::array<double, voiceCount> semitones{0, -24, -12, 7, 12, 24, 12, 24};
    for (std::size_t v = 0; v < voiceCount; ++v) {
      // Six long paths follow Warp; two warm short upper paths stay fixed.
      ratios_[v] = std::exp2(semitones[v] * (v >= 6 ? 1 : extent) / 12);
      for (std::size_t k = 0; k < bandCount; ++k) {
        // Heuristic source-tail taper, not an alias rejection proof. Output
        // phase work remains warm even for bands whose contribution is zero.
        const double edge = ratios_[v] * (design_[k].center + 4 * design_[k].bandwidth);
        const double t = std::clamp((edge - 18000) / 2000, 0.0, 1.0);
        gains_[k][v] = .5 + .5 * std::cos(std::numbers::pi * t);
      }
    }
  }

  void reset() noexcept {
    first_ = {}; second_ = {}; previous_ = {}; beforePrevious_ = {};
    for (auto& channel : phases_) for (auto& band : channel) band.reset();
  }

  Frame process(std::array<float, 2> input) noexcept {
    Frame result{};
    for (std::size_t c = 0; c < 2; ++c) {
      const float difference = input[c] - beforePrevious_[c];
      beforePrevious_[c] = previous_[c]; previous_[c] = input[c];
      for (std::size_t k = 0; k < bandCount; ++k) {
        first_[c][k] = poles_[k] * first_[c][k] + numerators_[k] * difference;
        second_[c][k] = poles_[k] * second_[c][k] + first_[c][k];
        const auto samples = phases_[c][k].template process<Lookup>(std::complex<double>(second_[c][k]), ratios_, tables_);
        for (std::size_t v = 0; v < voiceCount; ++v)
          result[v][c] += static_cast<float>(samples[v] * gains_[k][v]);
      }
    }
    return result;
  }

  const std::array<Design, bandCount>& design() const noexcept { return design_; }
  const ErbPhaseVoices::Ratios& ratios() const noexcept { return ratios_; }

private:
  std::array<Design, bandCount> design_{};
  std::array<std::complex<float>, bandCount> poles_{};
  std::array<float, bandCount> numerators_{};
  std::array<std::array<std::complex<float>, bandCount>, 2> first_{}, second_{};
  std::array<std::array<ErbPhaseVoices, bandCount>, 2> phases_{};
  std::array<ErbPhaseVoices::Ratios, bandCount> gains_{};
  ErbPhaseVoices::Ratios ratios_{};
  std::array<float, 2> previous_{}, beforePrevious_{};
  const ErbPhaseTables* tables_ = nullptr;
};
}
