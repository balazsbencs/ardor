#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

namespace pog3_trial {

// Isolated octave-up reference. Equations 37--40/table 6 specify the bands;
// table 5/figure 15 specify the prototype and positive-frequency pole mirror.
// Center gain 2 converts a real sinusoid into an analytic carrier of the same
// amplitude. This explicit normalization is our choice; the thesis does not
// publish its Matlab gain code. No decimation, approximate roots or adaptation.
class ErbPs2Reference {
public:
  static constexpr std::size_t bandCount = 43;
  static constexpr double sampleRate = 48000;
  struct Design {
    double center = 0, bandwidth = 0, numerator = 0;
    std::complex<double> pole{};
  };

  ErbPs2Reference() {
    for (std::size_t k = 0; k < bandCount; ++k) {
      auto& d = design_[k];
      const double erb = 5 + k * (4.0 / 6);
      const double target = 228.7 * (std::pow(10.0, erb / 21.3) - 1);
      d.center = target / 2;
      d.bandwidth = (24.7 + .108 * target) / 12;
      const double q = d.center / d.bandwidth / 2; // real prototype Q is half
      const double kBq = std::tan(std::numbers::pi * d.center / sampleRate);
      const double denominator = kBq * kBq * q + kBq + q;
      const double a1 = 2 * q * (kBq * kBq - 1) / denominator;
      const double a2 = (kBq * kBq * q - kBq + q) / denominator;
      // Both conjugate prototype poles map to this positive-imaginary root.
      const double real = -a1 / 2;
      d.pole = {real, std::sqrt(a2 - real * real)};
      const auto z = std::polar(1.0, -2 * std::numbers::pi * d.center / sampleRate);
      // Transfer: numerator*(1-z^-2)/(1-p*z^-1)^2.
      d.numerator = 2 * std::norm(1.0 - d.pole * z) / std::abs(1.0 - z * z);
      poles_[k] = {static_cast<float>(d.pole.real()), static_cast<float>(d.pole.imag())};
      numerators_[k] = static_cast<float>(d.numerator);
    }
  }

  void reset() noexcept { first_ = {}; second_ = {}; previous_ = {}; beforePrevious_ = {}; }
  const std::array<Design, bandCount>& design() const noexcept { return design_; }
  std::complex<float> analyticBand(std::size_t channel, std::size_t band) const noexcept {
    return second_[channel][band];
  }
  std::array<float, 2> process(std::array<float, 2> input) noexcept {
    std::array<float, 2> result{};
    for (std::size_t c = 0; c < 2; ++c) {
      const float difference = input[c] - beforePrevious_[c];
      beforePrevious_[c] = previous_[c]; previous_[c] = input[c];
      for (std::size_t k = 0; k < bandCount; ++k) {
        first_[c][k] = poles_[k] * first_[c][k] + numerators_[k] * difference;
        second_[c][k] = poles_[k] * second_[c][k] + first_[c][k];
        const double a = second_[c][k].real(), b = second_[c][k].imag();
        const double magnitude = std::sqrt(a * a + b * b);
        if (magnitude > 0) result[c] += static_cast<float>((a * a - b * b) / magnitude);
      }
    }
    return result;
  }

private:
  std::array<Design, bandCount> design_{};
  std::array<std::complex<float>, bandCount> poles_{};
  std::array<float, bandCount> numerators_{};
  std::array<std::array<std::complex<float>, bandCount>, 2> first_{}, second_{};
  std::array<float, 2> previous_{}, beforePrevious_{};
};
}
