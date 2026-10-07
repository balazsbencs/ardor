#include "erb_ps2_reference.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::complex<double> response(const pog3_trial::ErbPs2Reference::Design& d, double frequency) {
  const auto z = std::polar(1.0, -2 * std::numbers::pi * frequency / 48000);
  return d.numerator * (1.0 - z * z) / ((1.0 - d.pole * z) * (1.0 - d.pole * z));
}

void designAndImpulse() {
  pog3_trial::ErbPs2Reference reference;
  const auto& design = reference.design();
  require(design.front().center > 80 && design.front().center < 85, "leftmost analysis center");
  require(design.back().center > 3900 && design.back().center < 4000, "rightmost analysis center");
  for (const auto& d : design) {
    require(d.pole.imag() > 0 && std::abs(d.pole) < 1, "positive-frequency stable prototype pole");
    const std::complex<float> rounded{static_cast<float>(d.pole.real()), static_cast<float>(d.pole.imag())};
    require(std::abs(rounded) < 1, "prepared float pole remains stable");
    // Textbook trigonometric bandpass relations independently verify the
    // tan-based table-5 design's center and half-Q convention.
    const double a2 = std::norm(d.pole), omega = 2 * std::numbers::pi * d.center / 48000;
    const double alpha = (1 - a2) / (1 + a2);
    require(std::fabs(2 * d.pole.real() / (1 + a2) - std::cos(omega)) < 1e-12, "prototype bandpass center");
    require(std::fabs(alpha - std::sin(omega) * d.bandwidth / d.center) < 1e-12, "prototype half-Q convention");
    require(std::fabs(std::abs(response(d, d.center)) - 2) < 1e-9, "analytic center gain two");
    require(std::abs(response(d, -d.center)) < 2e-3, "negative-frequency rejection");
  }

  constexpr std::size_t n = 65536;
  const std::array<std::size_t, 3> selected{0, 21, 42};
  std::array<std::array<std::complex<double>, 5>, 3> projected{};
  std::array<std::array<double, 5>, 3> frequencies{};
  for (std::size_t b = 0; b < selected.size(); ++b) {
    const auto& d = design[selected[b]];
    frequencies[b] = {0, d.center, d.center - d.bandwidth, d.center + d.bandwidth, -d.center};
  }
  // DTFT of actual float-state impulse history checks magnitude and phase of
  // the cascade against the independently evaluated rational transfer. It also
  // catches the one/two-sample numerator history and section timing mistakes.
  for (std::size_t i = 0; i < n; ++i) {
    (void)reference.process({i == 0 ? 1.0f : 0.0f, 0});
    for (std::size_t b = 0; b < selected.size(); ++b) {
      const std::complex<double> sample = reference.analyticBand(0, selected[b]);
      require(reference.analyticBand(1, selected[b]) == std::complex<float>{}, "silent stereo channel stays silent");
      for (std::size_t f = 0; f < 5; ++f)
        projected[b][f] += sample * std::polar(1.0, -2 * std::numbers::pi * frequencies[b][f] * i / 48000);
    }
  }
  double worst = 0;
  for (std::size_t b = 0; b < selected.size(); ++b) for (std::size_t f = 0; f < 5; ++f) {
    const auto expected = response(design[selected[b]], frequencies[b][f]);
    const auto error = std::abs(projected[b][f] - expected);
    worst = std::max(worst, error);
    require(error < 1e-3, "actual float cascade matches complex transfer");
  }
  std::cout << "Three-band impulse DTFT worst absolute complex error=" << worst << '\n';
}

void stereoResetAndSustain() {
  pog3_trial::ErbPs2Reference stereo, left, right;
  double peak = 0;
  for (std::size_t i = 0; i < 4 * 48000; ++i) {
    const float l = .2 * std::sin(2 * std::numbers::pi * 329.628 * i / 48000);
    const float r = .13 * std::sin(2 * std::numbers::pi * 493.883 * i / 48000);
    const auto both = stereo.process({l, r});
    const auto a = left.process({l, 0}), b = right.process({0, r});
    require(both[0] == a[0] && both[1] == b[1], "independent stereo histories");
    for (const float sample : both) {
      require(std::isfinite(sample), "finite sustained output");
      peak = std::max(peak, static_cast<double>(std::fabs(sample)));
    }
  }
  stereo.reset();
  for (std::size_t i = 0; i < 48000; ++i)
    require(stereo.process({0, 0}) == std::array<float, 2>{}, "reset clears filter and input numerator histories");
  std::cout << "Stereo sustain/reset peak=" << peak << " core storage=" << sizeof(stereo) << " bytes\n";
}
}

int main() {
  try {
    designAndImpulse(); stereoResetAndSustain();
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
