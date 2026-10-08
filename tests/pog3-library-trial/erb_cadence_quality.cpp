#include "erb_cadence_bank.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr double pi = std::numbers::pi;
using Ratios = pog3_trial::ErbPhaseVoices::Ratios;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template<std::size_t Stride, bool CountCycles>
void constantCarrier(double center, double frequency, std::size_t prefix = 0) {
  pog3_trial::ErbCadenceVoices<Stride, CountCycles> renderer;
  renderer.prepare(center);
  const Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  double worst = 0;
  for (std::size_t i = 0; i < 48000; ++i) {
    const double inputPhase = .31 + 2 * pi * frequency * (static_cast<double>(i) - prefix) / 48000;
    const auto y = renderer.process(i < prefix ? std::complex<double>{} : std::polar(.2, inputPhase), ratios);
    for (std::size_t v = 0; v < 8; ++v) {
      const auto ready = ((prefix + Stride - 1) / Stride) * Stride + Stride;
      const double expected = i < ready ? 0 : .2 * std::cos(ratios[v] * (.31 + 2 * pi * frequency * (static_cast<double>(i) - prefix - Stride) / 48000));
      worst = std::max(worst, std::fabs(y[v] - expected));
    }
  }
  std::cout << "Carrier stride/count/center/frequency=" << Stride << '/' << CountCycles << '/' << center << '/' << frequency
    << " prefix=" << prefix << " worst absolute error=" << worst << '\n';
  require(worst < 4e-6, "delayed constant carrier phase/cycle count");
  renderer.reset();
  for (std::size_t i = 0; i < 100; ++i)
    require(renderer.process({}, ratios) == typename pog3_trial::ErbCadenceVoices<Stride, CountCycles>::Samples{}, "cadence reset silence");
}

void centerAmbiguity() {
  pog3_trial::ErbCadenceVoices<8, false> renderer;
  renderer.prepare(400);
  const Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  double intendedError = 0, foldedError = 0;
  for (std::size_t i = 0; i < 4096; ++i) {
    const auto y = renderer.process(std::polar(.2, .31 + 2 * pi * 5000 * i / 48000), ratios);
    if (i < 8) continue;
    const double t = static_cast<double>(i - 8) / 48000;
    intendedError = std::max(intendedError, std::fabs(y[2] - .2 * std::cos(.5 * (.31 + 2 * pi * 5000 * t))));
    foldedError = std::max(foldedError, std::fabs(y[2] - .2 * std::cos(.5 * (.31 - 2 * pi * 1000 * t))));
  }
  require(intendedError > .3 && foldedError < 4e-6, "center-only endpoints demonstrate wrong carrier winding");
  std::cout << "Center-only off-band intended/folded errors=" << intendedError << '/' << foldedError << '\n';
}

template<std::size_t Stride>
void movingAndTransient() {
  pog3_trial::ErbCadenceVoices<Stride, true> renderer;
  renderer.prepare(400);
  constexpr std::size_t count = 24000;
  std::vector<Ratios> history(count);
  Ratios phase{};
  Ratios previousRatios{};
  double inputPhase = .31, endpointError = 0, interiorError = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const double extent = i < count / 2 ? i / static_cast<double>(count / 2) : (count - i) / static_cast<double>(count / 2);
    const Ratios ratios{1, std::exp2(-2 * extent), std::exp2(-extent), std::exp2(7.0 / 12 * extent),
      std::exp2(extent), std::exp2(2 * extent), 2, 4};
    if (i) renderer.controlBoundary(previousRatios);
    previousRatios = ratios;
    const double delta = 2 * pi * (317.173 + 80 * std::sin(i * .00043)) / 48000;
    if (!i) for (std::size_t v = 0; v < 8; ++v) phase[v] = ratios[v] * inputPhase;
    else {
      inputPhase += delta;
      for (std::size_t v = 0; v < 8; ++v) phase[v] += ratios[v] * delta;
    }
    history[i] = phase;
    const auto y = renderer.process(std::polar(.2, inputPhase), ratios);
    if (i >= Stride) for (std::size_t v = 0; v < 8; ++v) {
      const double error = std::fabs(y[v] - .2 * std::cos(history[i - Stride][v]));
      if (i % Stride == 0) endpointError = std::max(endpointError, error);
      else interiorError = std::max(interiorError, error);
    }
  }
  require(endpointError < 4e-6, "counted phase endpoints preserve moving ratio integration");
  // Interpolated phase error grows with stride squared for this smooth chirp.
  // This is a prototype numerical bound, not the production audio gate.
  require(interiorError < 2e-4 * std::max(1.0, Stride * Stride / 256.0), "smooth chirp/Warp interpolation error bound");

  renderer.reset();
  const Ratios ratios{1, .25, .5, std::exp2(7.0 / 12), 2, 4, 2, 4};
  double transientError = 0;
  for (std::size_t i = 0; i < 1024; ++i) {
    const double amplitude = i < 103 ? .2 : .4;
    const auto y = renderer.process(std::polar(amplitude, .31 + 2 * pi * 317.173 * i / 48000), ratios);
    if (i < Stride) continue;
    for (std::size_t v = 0; v < 8; ++v) {
      const double expectedAmplitude = i - Stride < 103 ? .2 : .4;
      const double expected = expectedAmplitude * std::cos(ratios[v] * (.31 + 2 * pi * 317.173 * (i - Stride) / 48000));
      const double error = std::fabs(y[v] - expected);
      transientError = std::max(transientError, error);
      if (i < 103 || i >= 103 + 2 * Stride) require(error < 4e-6, "amplitude step difference remains near its interpolation interval");
    }
  }
  std::cout << "Stride=" << Stride << " moving endpoint/interior errors=" << endpointError << '/' << interiorError
    << " amplitude-step deviation=" << transientError << '\n';
}

template<std::size_t Stride>
auto partitioned(const std::vector<std::size_t>& partitions) {
  pog3_trial::ErbCadenceBank<Stride, true> bank;
  std::vector<typename pog3_trial::ErbCadenceBank<Stride, true>::Frame> output(4096);
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

void stereoAndBank() {
  pog3_trial::ErbCadenceBank<8, true> both, left, right;
  for (std::size_t i = 0; i < 8192; ++i) {
    const float l = .2 * std::sin(2 * pi * 329.628 * i / 48000);
    const float r = .13 * std::sin(2 * pi * 493.883 * i / 48000);
    const auto y = both.process({l, r}), a = left.process({l, 0}), b = right.process({0, r});
    for (std::size_t v = 0; v < 8; ++v) {
      require(y[v][0] == a[v][0] && y[v][1] == b[v][1], "cadence stereo independence");
      require(std::isfinite(y[v][0]) && std::isfinite(y[v][1]), "cadence finite output");
    }
  }
  both.reset();
  for (std::size_t i = 0; i < 1024; ++i)
    require(both.process({0, 0}) == pog3_trial::ErbCadenceBank<8, true>::Frame{}, "bank reset includes delayed oscillators");
  require(partitioned<4>({64}) == partitioned<4>({1, 17, 63, 128, 5}), "stride four partitions");
  require(partitioned<8>({128}) == partitioned<8>({1, 17, 63, 64, 5}), "stride eight partitions");
  require(partitioned<16>({128}) == partitioned<16>({1, 17, 63, 64, 5}), "stride sixteen partitions");
  require(partitioned<32>({128}) == partitioned<32>({1, 17, 63, 64, 5}), "stride thirty-two partitions");
  std::cout << "Cadence counted bank storage=" << sizeof(both) << " bytes\n";
  std::cout << "Cadence stride-32 bank storage=" << sizeof(pog3_trial::ErbCadenceBank<32, true>) << " bytes\n";
}

void coefficientAccuracy() {
  double worst = 0;
  for (std::size_t i = 0; i <= 100000; ++i) {
    const double phase = -pi + 2 * pi * i / 100000;
    worst = std::max(worst, std::fabs(pog3_trial::erbRecurrenceCos(phase) - std::cos(phase)));
  }
  require(worst < 4e-9, "degree-18 recurrence cosine error");
  std::cout << "Recurrence cosine worst absolute error=" << worst << '\n';
}

template<std::size_t Stride>
void delayedComparison() {
  pog3_trial::ErbSharedBank<true> reference;
  pog3_trial::ErbCadenceBank<Stride, true> cadence;
  std::array<pog3_trial::ErbSharedBank<true>::Frame, Stride> delay{};
  std::array<double, 8> error{}, energy{};
  for (std::size_t i = 0; i < 24000; ++i) {
    const double t = static_cast<double>(i) / 48000;
    double x = .2 * std::exp(-static_cast<double>(i) / 8000) * std::sin(2 * pi * 196 * t);
    if (i >= 6000) x += .13 * std::exp(-static_cast<double>(i - 6000) / 5000) * std::sin(2 * pi * 493.883 * t);
    const std::array<float, 2> input{static_cast<float>(x), static_cast<float>(-.71 * x)};
    const auto expected = delay[i % Stride];
    delay[i % Stride] = reference.process(input);
    const auto y = cadence.process(input);
    if (i >= 2 * Stride) for (std::size_t v = 0; v < 8; ++v) for (std::size_t c = 0; c < 2; ++c) {
      require(std::isfinite(y[v][c]), "finite transient comparison");
      const double difference = y[v][c] - expected[v][c];
      error[v] += difference * difference;
      energy[v] += static_cast<double>(expected[v][c]) * expected[v][c];
    }
  }
  for (std::size_t v = 0; v < 8; ++v) {
    require(energy[v] > 1e-6, "nonempty accurate transient control");
    // Observational DSP error, not an assertion that the polyphonic reference
    // or its cadence approximation has passed production quality gates.
    std::cout << "Transient comparison stride/voice=" << Stride << '/' << v
      << " error/reference power dB=" << 10 * std::log10(std::max(error[v] / energy[v], 1e-30)) << '\n';
  }
}
}

int main() {
  try {
    coefficientAccuracy();
    constantCarrier<8, false>(400, 317.173);
    constantCarrier<4, true>(400, 21173.125);
    constantCarrier<8, true>(400, 5000);
    constantCarrier<16, true>(400, 21173.125);
    constantCarrier<16, true>(400, 317.173);
    constantCarrier<32, true>(400, 317.173);
    constantCarrier<32, true>(400, 21173.125);
    constantCarrier<32, true>(400, 21173.125, 7);
    constantCarrier<32, true>(400, .173);
    constantCarrier<32, true>(400, 6000);
    constantCarrier<32, true>(400, 23999.9);
    constantCarrier<32, true>(400, -5000);
    constantCarrier<32, true>(400, -23999.9);
    centerAmbiguity();
    movingAndTransient<4>(); movingAndTransient<8>(); movingAndTransient<16>();
    movingAndTransient<32>();
    stereoAndBank(); delayedComparison<8>(); delayedComparison<32>(); return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
