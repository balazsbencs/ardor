#include "cheese/CheeseCircuit.h"
#include "cheese/CheeseDk.h"
#include "cheese/CheeseNetlist.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

constexpr float kOversampled = 192000.0f;
constexpr double kTwoPi = 6.28318530718;

void verifyNetlistValidation()
{
  require(ardor::cheeseNetlistValid(ardor::cheeseNetlist()),
          "the shipped netlist must be realizable");
  auto broken = ardor::cheeseNetlist();
  broken.c9Farads = 0.0;
  require(!ardor::cheeseNetlistValid(broken), "a zero capacitor must be rejected");
  broken = ardor::cheeseNetlist();
  broken.r5Ohms = -1.0;
  require(!ardor::cheeseNetlistValid(broken), "a negative resistor must be rejected");
}

// Every node has to sit somewhere a real circuit could put it. This is the
// check that catches a stamping error, because a wrong sign or a swapped node
// almost always parks a transistor outside its supply.
void verifyOperatingPointIsPhysical()
{
  const auto& netlist = ardor::cheeseNetlist();
  const auto point = ardor::cheeseOperatingPoint(netlist, {0.7, 0.5});

  const double q1Collector = ardor::cheeseQ1CollectorVolts(point);
  const double q2Collector = ardor::cheeseQ2CollectorVolts(point);
  require(q1Collector > 0.2 && q1Collector < netlist.supplyVolts,
          "Q1 must sit between saturation and the rail; found "
            + std::to_string(q1Collector) + " V");
  require(q2Collector > 0.2 && q2Collector < netlist.supplyVolts,
          "Q2 must sit between saturation and the rail; found "
            + std::to_string(q2Collector) + " V");

  for (const double vbe : {ardor::cheeseQ1BaseEmitterVolts(point),
                           ardor::cheeseQ2BaseEmitterVolts(point)}) {
    require(vbe > 0.35 && vbe < 0.75,
            "a conducting silicon base-emitter junction must sit near half a volt; found "
              + std::to_string(vbe) + " V");
  }
}

// A real circuit fact worth holding onto: the 4.7 uF across the Fuzz pot's lower
// leg blocks DC, so the bias sees the whole 1 k track wherever the wiper is.
// That is why this family of fuzz can be turned down without going out of bias,
// and a stamping change that broke it would otherwise pass unnoticed.
void verifyBiasDoesNotFollowTheFuzzControl()
{
  const auto& netlist = ardor::cheeseNetlist();
  const auto atZero = ardor::cheeseOperatingPoint(netlist, {0.0, 0.5});
  const auto atFull = ardor::cheeseOperatingPoint(netlist, {1.0, 0.5});
  require(std::fabs(ardor::cheeseQ2CollectorVolts(atZero)
                    - ardor::cheeseQ2CollectorVolts(atFull)) < 1.0e-6,
          "the Fuzz control must not move the bias point");
}

// The derived recurrence has to be stable on its own, before any device is
// attached. Estimated by repeated squaring rather than power iteration, because
// this circuit's dominant modes are a complex pair and a per-step norm would
// oscillate instead of converging.
void verifyDerivedSystemIsStable()
{
  const auto matrices = ardor::deriveCheeseDk(ardor::cheeseNetlist(), {0.85, 0.5}, kOversampled);
  const std::size_t n = matrices.states;
  require(n > 0 && matrices.ports == 3, "the derivation must produce three ports");

  std::vector<double> a = matrices.a;
  double logScale = 0.0;
  constexpr int kSquarings = 20;
  for (int step = 0; step < kSquarings; ++step) {
    std::vector<double> next(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t k = 0; k < n; ++k) {
        const double aik = a[i * n + k];
        if (aik == 0.0) continue;
        for (std::size_t j = 0; j < n; ++j) next[i * n + j] += aik * a[k * n + j];
      }
    }
    double norm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      double row = 0.0;
      for (std::size_t j = 0; j < n; ++j) row += std::fabs(next[i * n + j]);
      norm = std::max(norm, row);
    }
    require(norm > 0.0, "the state matrix must not collapse to zero");
    for (auto& value : next) value /= norm;
    logScale = 2.0 * logScale + std::log(norm);
    a.swap(next);
  }
  const double radius = std::exp(logScale / static_cast<double>(1 << kSquarings));
  require(radius < 1.0,
          "the derived state matrix must be stable; spectral radius "
            + std::to_string(radius));
}

struct Rendered {
  double peak = 0.0;
  double rms = 0.0;
  double clipperPeak = 0.0;
};

Rendered render(float fuzz, float tone, float amplitude, float frequency = 220.0f)
{
  ardor::CheeseCircuit circuit;
  circuit.init(ardor::cheeseNetlist(), kOversampled);
  circuit.setControls(fuzz, tone, 1.0f);
  circuit.reset();

  Rendered out;
  const int frames = static_cast<int>(kOversampled * 0.3f);
  int counted = 0;
  double energy = 0.0;
  for (int n = 0; n < frames; ++n) {
    const float x = amplitude * std::sin(static_cast<float>(kTwoPi) * frequency * n / kOversampled);
    const double y = circuit.process(x);
    if (n < frames / 2) continue;
    out.peak = std::max(out.peak, std::fabs(y));
    out.clipperPeak = std::max(out.clipperPeak, static_cast<double>(std::fabs(circuit.clipperVolts())));
    energy += y * y;
    ++counted;
  }
  out.rms = std::sqrt(energy / counted);
  return out;
}

// A fuzz compresses: the loudest input is barely louder out than a quiet one.
// That behaviour is the circuit's, not a limiter's, and it is what says the
// transistors are actually being driven.
void verifyItCompressesWithPlayingLevel()
{
  const auto quiet = render(0.7f, 0.5f, 0.01f);
  const auto loud = render(0.7f, 0.5f, 0.5f);

  const double quietGainDb = 20.0 * std::log10(quiet.rms / (0.01 * 0.70710678));
  const double loudGainDb = 20.0 * std::log10(loud.rms / (0.5 * 0.70710678));
  require(quietGainDb - loudGainDb > 20.0,
          "a fifty-fold louder input must not come out fifty times louder; gains "
            + std::to_string(quietGainDb) + " and " + std::to_string(loudGainDb) + " dB");
  require(loud.rms > quiet.rms, "louder in must still be louder out");
}

// The clipping node is held by silicon, so it lands where silicon lands however
// hard it is driven.
void verifyClipperHoldsAtASiliconDrop()
{
  const auto driven = render(0.9f, 0.5f, 0.3f);
  require(driven.clipperPeak > 0.35 && driven.clipperPeak < 0.75,
          "the clipping node must sit at a silicon drop; measured "
            + std::to_string(driven.clipperPeak) + " V");
}

// The tone stack has to actually sweep, and in the right direction: up is
// brighter.
void verifyToneSweeps()
{
  const auto darkLow = render(0.4f, 0.0f, 0.05f, 200.0f);
  const auto darkHigh = render(0.4f, 0.0f, 0.05f, 3000.0f);
  const auto brightLow = render(0.4f, 1.0f, 0.05f, 200.0f);
  const auto brightHigh = render(0.4f, 1.0f, 0.05f, 3000.0f);

  const double darkTilt = 20.0 * std::log10(darkHigh.rms / darkLow.rms);
  const double brightTilt = 20.0 * std::log10(brightHigh.rms / brightLow.rms);
  require(brightTilt > darkTilt + 6.0,
          "turning the tone up must tilt towards treble; tilts "
            + std::to_string(darkTilt) + " and " + std::to_string(brightTilt) + " dB");
}

void verifyOutputStaysFinite()
{
  ardor::CheeseCircuit circuit;
  circuit.init(ardor::cheeseNetlist(), kOversampled);
  circuit.setControls(1.0f, 0.5f, 0.7f);
  circuit.reset();
  for (int n = 0; n < static_cast<int>(kOversampled); ++n) {
    const float x = 0.4f * std::sin(static_cast<float>(kTwoPi) * 110.0f * n / kOversampled)
      + 0.2f * std::sin(static_cast<float>(kTwoPi) * 1730.0f * n / kOversampled);
    const float y = circuit.process(x);
    require(std::isfinite(y), "the output must stay finite");
    require(std::fabs(y) < 20.0f,
            "the output must stay inside the supply rails; reached " + std::to_string(y));
  }
}

} // namespace

int main()
{
  verifyNetlistValidation();
  verifyOperatingPointIsPhysical();
  verifyBiasDoesNotFollowTheFuzzControl();
  verifyDerivedSystemIsStable();
  verifyClipperHoldsAtASiliconDrop();
  verifyItCompressesWithPlayingLevel();
  verifyToneSweeps();
  verifyOutputStaysFinite();
  std::printf("cheese smoke passed\n");
  return 0;
}
