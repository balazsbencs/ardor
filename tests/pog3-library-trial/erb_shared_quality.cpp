#include "erb_shared_bank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
constexpr double pi = std::numbers::pi;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template<bool Lookup>
void carrierPhase() {
  pog3_trial::ErbPhaseVoices renderer;
  const auto& tables = pog3_trial::erbPhaseTables();
  pog3_trial::ErbPhaseVoices::Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  auto expectedPhase = ratios;
  double inputPhase = -.71, worst = 0;
  for (std::size_t v = 0; v < ratios.size(); ++v) expectedPhase[v] = ratios[v] * inputPhase;
  // Includes a fifth away from any analysis center, downward branch crossings,
  // two Warp ramp reversals, and near-Nyquist input phase increments. The
  // oracle integrates known unwrapped input increments; it does not inspect
  // implementation phase histories or multiply input phase by a moving ratio.
  for (std::size_t i = 0; i < 200000; ++i) {
    if (i == 48000) ratios = {1, .41, .71, 1.28, 1.67, 2.8, 2, 4};
    if (i >= 50000 && i < 100000) {
      const double extent = (i - 50000) / 50000.0;
      ratios[1] = std::exp2(-2 * extent);
      ratios[2] = std::exp2(-extent);
      ratios[3] = std::exp2(7.0 / 12 * extent);
      ratios[4] = std::exp2(extent); ratios[5] = std::exp2(2 * extent);
    }
    if (i >= 100000 && i < 150000) {
      const double extent = (150000 - i) / 50000.0;
      ratios[1] = std::exp2(-2 * extent);
      ratios[2] = std::exp2(-extent);
      ratios[3] = std::exp2(7.0 / 12 * extent);
      ratios[4] = std::exp2(extent); ratios[5] = std::exp2(2 * extent);
    }
    const double delta = 2 * pi * (i < 150000 ? 317.173 : 21173.125) / 48000;
    if (i) {
      inputPhase += delta;
      for (std::size_t v = 0; v < ratios.size(); ++v) expectedPhase[v] += ratios[v] * delta;
    }
    const double magnitude = .15 + .08 * std::sin(i * .0031);
    const auto output = renderer.process<Lookup>(std::polar(magnitude, inputPhase), ratios, &tables);
    for (std::size_t v = 0; v < ratios.size(); ++v) {
      const double error = std::fabs(output[v] - magnitude * std::cos(expectedPhase[v]));
      worst = std::max(worst, error);
      require(error < 5e-7, "phase integration matches unwrapped independent oracle");
    }
  }
  require(renderer.process<Lookup>({}, ratios, &tables) == pog3_trial::ErbPhaseVoices::Samples{}, "silence emits zero");
  require(renderer.process<Lookup>({1e-25, -1e-25}, ratios, &tables) == pog3_trial::ErbPhaseVoices::Samples{}, "unmeasurable carrier phase floor");
  const auto reacquired = renderer.process<Lookup>(std::polar(.2, .37), ratios, &tables);
  for (std::size_t v = 0; v < ratios.size(); ++v)
    require(std::fabs(reacquired[v] - .2 * std::cos(.37 * ratios[v])) < (Lookup ? 3e-7 : 1e-14), "phase reacquisition after silence");
  renderer.reset();
  require(renderer.process({}, ratios) == pog3_trial::ErbPhaseVoices::Samples{}, "phase reset");
  std::cout << "Carrier " << (Lookup ? "lookup" : "accurate") << " static/ramp/reversal oracle worst absolute error=" << worst << '\n';
}

void tableAccuracy() {
  const auto& tables = pog3_trial::erbPhaseTables();
  double angleError = 0, cosineError = 0;
  for (std::size_t i = 0; i <= 500000; ++i) {
    const double phase = -pi + 2 * pi * i / 500000;
    const auto carrier = std::polar(1.0, phase);
    angleError = std::max(angleError, std::fabs(std::remainder(tables.angle(carrier) - phase, 2 * pi)));
    cosineError = std::max(cosineError, std::fabs(tables.cos(phase) - std::cos(phase)));
  }
  require(angleError < 8e-8, "lookup input angle absolute error");
  require(cosineError < 3.3e-7, "lookup carrier cosine absolute error");
  std::cout << "Lookup angle/cosine worst errors=" << angleError << '/' << cosineError << '\n';
}

void referenceAndStereo() {
  pog3_trial::ErbPs2Reference reference;
  pog3_trial::ErbSharedBank<false> shared, left, right;
  double worst = 0;
  for (std::size_t i = 0; i < 48000; ++i) {
    const std::array<float, 2> input{
      static_cast<float>(.2 * std::sin(2 * pi * 329.628 * i / 48000)),
      static_cast<float>(.13 * std::sin(2 * pi * 493.883 * i / 48000))};
    const auto expected = reference.process(input);
    const auto both = shared.process(input);
    const auto a = left.process({input[0], 0}), b = right.process({0, input[1]});
    for (std::size_t c = 0; c < 2; ++c) worst = std::max(worst, std::fabs(static_cast<double>(both[4][c] - expected[c])));
    for (std::size_t v = 0; v < shared.voiceCount; ++v) {
      require(both[v][0] == a[v][0] && both[v][1] == b[v][1], "shared bank stereo histories");
      require(std::isfinite(both[v][0]) && std::isfinite(both[v][1]), "finite shared outputs");
    }
  }
  require(worst < 2e-6, "shared static +12 agrees with untouched raw reference");
  shared.reset();
  for (std::size_t i = 0; i < 1000; ++i)
    require(shared.process({0, 0}) == pog3_trial::ErbSharedBank<false>::Frame{}, "all shared histories reset");
  std::cout << "Shared +12 reference worst absolute error=" << worst << '\n';
}

template<bool Wide, bool Lookup = false>
auto partitioned(const std::vector<std::size_t>& partitions) {
  pog3_trial::ErbSharedBank<Wide, Lookup> bank;
  std::vector<typename pog3_trial::ErbSharedBank<Wide, Lookup>::Frame> output(4096);
  std::size_t sample = 0, block = 0;
  while (sample < output.size()) {
    const auto end = std::min(output.size(), sample + partitions[block++ % partitions.size()]);
    for (; sample < end; ++sample) {
      if (sample % 48 == 0) bank.setWarp(static_cast<double>((sample / 48) % 17) / 16);
      const float x = .1 * std::sin(2 * pi * 223.718 * sample / 48000);
      output[sample] = bank.process({x, -.71f * x});
    }
  }
  return output;
}

void wideAndPartitions() {
  pog3_trial::ErbSharedBank<true> wide;
  const auto& design = wide.design();
  require(design.front().center > 35 && design.front().center < 36, "wide low center");
  require(design.back().center > 20000 && design.back().center < 20100, "wide high center");
  for (const auto& d : design) {
    const std::complex<float> p{static_cast<float>(d.pole.real()), static_cast<float>(d.pole.imag())};
    require(p.imag() > 0 && std::abs(p) < 1, "wide rounded pole stable");
    require(std::isfinite(d.numerator) && d.numerator > 0, "wide numerator finite");
  }
  wide.setWarp(.35);
  const auto ratios = wide.ratios();
  require(std::fabs(ratios[4] - std::exp2(.35)) < 1e-14 && ratios[6] == 2 && ratios[7] == 4,
    "six Warp paths and two fixed upper paths");
  require(partitioned<false>({64}) == partitioned<false>({1, 17, 63, 128, 5}), "paper-grid partition invariance");
  require(partitioned<true>({128}) == partitioned<true>({1, 17, 63, 64, 5}), "wide-grid partition invariance");
  require(partitioned<true, true>({128}) == partitioned<true, true>({1, 17, 63, 64, 5}), "lookup wide-grid partition invariance");
  const auto exact = partitioned<true>({128}), lookup = partitioned<true, true>({128});
  double worst = 0;
  for (std::size_t i = 0; i < exact.size(); ++i) for (std::size_t v = 0; v < wide.voiceCount; ++v)
    for (std::size_t c = 0; c < 2; ++c)
      worst = std::max(worst, std::fabs(static_cast<double>(lookup[i][v][c] - exact[i][v][c])));
  require(worst < 2e-6, "lookup bank agrees with accurate bank during Warp controls");
  std::cout << "Lookup wide bank automated output worst absolute difference=" << worst << '\n';
  std::cout << "Wide centers=" << design.front().center << ".." << design.back().center
    << " Hz; core storage paper/wide=" << sizeof(pog3_trial::ErbSharedBank<false>) << '/' << sizeof(wide) << " bytes\n";
}
}

int main() {
  try { tableAccuracy(); carrierPhase<false>(); carrierPhase<true>(); referenceAndStereo(); wideAndPartitions(); return 0; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
