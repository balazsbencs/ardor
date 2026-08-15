#include "rat/RatCircuit.h"
#include "rat/RatNetlist.h"
#include "rat/RatProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>
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

// Magnitude response at one frequency, measured by quadrature over a whole
// number of cycles. `pick` chooses which node of the circuit to look at.
template <typename Pick>
double responseAt(ardor::RatCircuit& circuit, float frequency, float amplitude, Pick pick)
{
  const int warm = static_cast<int>(kOversampled * 0.5f);
  const int measure = static_cast<int>(kOversampled * 0.5f);
  for (int n = 0; n < warm; ++n) {
    circuit.process(amplitude * std::sin(static_cast<float>(kTwoPi) * frequency * n / kOversampled));
  }
  double real = 0.0;
  double imaginary = 0.0;
  for (int n = 0; n < measure; ++n) {
    const float phase = static_cast<float>(kTwoPi) * frequency * (warm + n) / kOversampled;
    circuit.process(amplitude * std::sin(phase));
    const double y = pick(circuit);
    real += y * std::cos(kTwoPi * frequency * n / kOversampled);
    imaginary += y * std::sin(kTwoPi * frequency * n / kOversampled);
  }
  return 2.0 * std::hypot(real, imaginary) / measure / amplitude;
}

double stageResponse(ardor::RatCircuit& circuit, float frequency, float amplitude)
{
  return responseAt(circuit, frequency, amplitude,
                    [](const ardor::RatCircuit& c) { return static_cast<double>(c.stageOutputVolts()); });
}

// The textbook non-inverting gain of the stage: 1 + Zf / Z, with Zf the
// distortion pot in parallel with C9 and Z the two capacitively coupled legs in
// parallel. This is the thing the discrete model has to agree with.
double idealStageGain(const ardor::RatNetlist& n, double r9Ohms, double frequency)
{
  using Complex = std::complex<double>;
  const double omega = kTwoPi * frequency;
  const Complex z7(n.r7Ohms, -1.0 / (omega * n.c7Farads));
  const Complex z8(n.r8Ohms, -1.0 / (omega * n.c8Farads));
  const Complex z = z7 * z8 / (z7 + z8);
  const Complex zc9(0.0, -1.0 / (omega * n.c9Farads));
  const Complex zf = Complex(r9Ohms, 0.0) * zc9 / (Complex(r9Ohms, 0.0) + zc9);
  return std::abs(1.0 + zf / z);
}

// With the op-amp made fast enough to be out of the way, the discrete solve of
// the feedback loop has to land on the analytic response. This is what says the
// companion models and the loop solve are right, rather than merely plausible.
void verifyGainStageMatchesTheory()
{
  auto netlist = ardor::ratNetlist();
  netlist.gbwHz = 200.0e6;
  netlist.slewVoltsPerSec = 200.0e6;

  for (const float distortion : {0.5f, 1.0f}) {
    const double r9 = ardor::ratPotOhms(netlist, netlist.r9Ohms, distortion);
    for (const float frequency : {50.0f, 200.0f, 1000.0f, 5000.0f}) {
      ardor::RatCircuit circuit;
      circuit.init(netlist, kOversampled);
      circuit.setControls(distortion, 0.0f, 1.0f);
      circuit.reset();
      const double measured = stageResponse(circuit, frequency, 1.0e-5f);
      const double expected = idealStageGain(netlist, r9, frequency);
      const double errorDb = 20.0 * std::log10(measured / expected);
      require(std::fabs(errorDb) < 0.3,
              "gain stage must match the analytic response; at " + std::to_string(frequency)
                + " Hz, distortion " + std::to_string(distortion) + ", error "
                + std::to_string(errorDb) + " dB");
    }
  }
}

// The RAT's gain rises with frequency because both feedback legs are
// capacitively coupled. That tilt is the pedal's voice, so it is asserted
// directly rather than left to follow from the formula.
void verifyGainRisesWithFrequency()
{
  const auto& netlist = ardor::ratNetlist();
  ardor::RatCircuit circuit;
  circuit.init(netlist, kOversampled);
  circuit.setControls(1.0f, 0.0f, 1.0f);

  circuit.reset();
  const double atFifty = stageResponse(circuit, 50.0f, 1.0e-5f);
  circuit.reset();
  const double atOneKilohertz = stageResponse(circuit, 1000.0f, 1.0e-5f);

  const double tiltDb = 20.0 * std::log10(atOneKilohertz / atFifty);
  require(tiltDb > 12.0,
          "the gain stage must be much louder at 1 kHz than at 50 Hz; measured "
            + std::to_string(tiltDb) + " dB of tilt");
  require(20.0 * std::log10(atOneKilohertz) > 60.0,
          "full distortion must reach the pedal's stated gain");
}

// Distortion at the bottom of its travel is a unity-gain follower, so the stage
// must be clean and flat there.
void verifyMinimumDistortionIsUnity()
{
  const auto& netlist = ardor::ratNetlist();
  ardor::RatCircuit circuit;
  circuit.init(netlist, kOversampled);
  circuit.setControls(0.0f, 0.0f, 1.0f);
  for (const float frequency : {100.0f, 1000.0f, 5000.0f}) {
    circuit.reset();
    const double gainDb = 20.0 * std::log10(stageResponse(circuit, frequency, 0.05f));
    require(std::fabs(gainDb) < 0.5,
            "minimum distortion must be unity gain; at " + std::to_string(frequency)
              + " Hz the stage gave " + std::to_string(gainDb) + " dB");
  }
}

// The op-amp cannot move faster than the part does, whatever it is asked for.
// This is not a detail: at 0.3 V/us the LM308 rounds off hard attacks before
// the diodes ever see them, and that is a large part of why a RAT sounds like
// one.
void verifyOpampSlewLimits()
{
  const auto& netlist = ardor::ratNetlist();
  ardor::RatCircuit circuit;
  circuit.init(netlist, kOversampled);
  circuit.setControls(1.0f, 0.0f, 1.0f);
  circuit.reset();

  const int halfPeriod = static_cast<int>(kOversampled / 2000.0f);
  double previous = 0.0;
  double fastest = 0.0;
  for (int n = 0; n < static_cast<int>(kOversampled * 0.05f); ++n) {
    circuit.process((n / halfPeriod) % 2 == 0 ? 0.05f : -0.05f);
    const double value = circuit.stageOutputVolts();
    if (n > static_cast<int>(kOversampled * 0.02f)) {
      fastest = std::max(fastest, std::fabs(value - previous) * kOversampled);
    }
    previous = value;
  }
  require(fastest <= netlist.slewVoltsPerSec * 1.02,
          "the op-amp must not move faster than the part can; measured "
            + std::to_string(fastest * 1.0e-6) + " V/us");
  require(fastest > netlist.slewVoltsPerSec * 0.7,
          "a square wave at full gain must actually reach the slew limit; measured "
            + std::to_string(fastest * 1.0e-6) + " V/us");
}

// The diodes are a matched antiparallel pair, so the clipping level is set by
// them and is the same either way round.
void verifyDiodesClipSymmetrically()
{
  const auto& netlist = ardor::ratNetlist();
  ardor::RatCircuit circuit;
  circuit.init(netlist, kOversampled);
  circuit.setControls(1.0f, 0.0f, 1.0f);
  circuit.reset();

  double positive = 0.0;
  double negative = 0.0;
  const int frames = static_cast<int>(kOversampled * 0.5f);
  for (int n = 0; n < frames; ++n) {
    const float x = 0.2f * std::sin(static_cast<float>(kTwoPi) * 440.0f * n / kOversampled);
    const double y = circuit.process(x);
    if (n < frames / 2) continue;
    positive = std::max(positive, y);
    negative = std::min(negative, y);
  }
  require(positive > 0.4 && positive < 0.9,
          "a hard-driven output must sit at the diode drop; measured "
            + std::to_string(positive) + " V");
  require(std::fabs(positive + negative) < 0.05 * positive,
          "a matched pair must clip both halves alike; measured +"
            + std::to_string(positive) + " and " + std::to_string(negative));
}

// The filter is a series resistance into a shunt, so its corner runs from
// 32 kHz at the bright end down to 475 Hz at the dark end.
void verifyFilterCorners()
{
  const auto& netlist = ardor::ratNetlist();
  const auto through = [&netlist](float filter, float frequency) {
    ardor::RatCircuit circuit;
    circuit.init(netlist, kOversampled);
    circuit.setControls(0.0f, filter, 1.0f);
    circuit.reset();
    const int warm = static_cast<int>(kOversampled * 0.4f);
    const int measured = static_cast<int>(kOversampled * 0.4f);
    for (int n = 0; n < warm; ++n) {
      circuit.process(0.01f * std::sin(static_cast<float>(kTwoPi) * frequency * n / kOversampled));
    }
    double real = 0.0;
    double imaginary = 0.0;
    for (int n = 0; n < measured; ++n) {
      const float phase = static_cast<float>(kTwoPi) * frequency * (warm + n) / kOversampled;
      const double y = circuit.process(0.01f * std::sin(phase));
      real += y * std::cos(kTwoPi * frequency * n / kOversampled);
      imaginary += y * std::sin(kTwoPi * frequency * n / kOversampled);
    }
    return 2.0 * std::hypot(real, imaginary) / measured / 0.01;
  };

  // Dark end: a first-order corner at 475 Hz predicts these within a fraction
  // of a dB, so they are checked against the formula rather than a snapshot.
  const double darkCorner = 1.0 / (kTwoPi * (netlist.r17Ohms + netlist.r15Ohms) * netlist.c11Farads);
  for (const float frequency : {1000.0f, 5000.0f}) {
    const double expected = -20.0 * std::log10(std::hypot(1.0, frequency / darkCorner));
    const double measured = 20.0 * std::log10(through(1.0f, frequency));
    require(std::fabs(measured - expected) < 0.6,
            "the dark end of the filter must follow its RC corner; at "
              + std::to_string(frequency) + " Hz expected " + std::to_string(expected)
              + " dB but measured " + std::to_string(measured));
  }

  // Bright end: the pot is out of circuit and R15 alone puts the corner above
  // the audio band, so nothing should be lost.
  require(std::fabs(20.0 * std::log10(through(0.0f, 5000.0f))) < 0.5,
          "the bright end of the filter must be effectively flat");
}

// A hard-driven diode pair makes harmonics far above the audio band, and the
// oversampling is the only thing keeping them from folding back into it. This
// is measured rather than assumed: a 5 kHz tone can only produce energy at
// multiples of 5 kHz, so anything found in a bin that is not a multiple is
// aliasing and nothing else. The worst case is checked, which is full
// distortion with the filter at its brightest.
void verifyOversamplingSuppressesAliasing()
{
  constexpr float kHostRate = 48000.0f;
  constexpr float kTone = 5000.0f;

  ardor::RatProcessor processor;
  std::string error;
  nlohmann::json params;
  params["mode"] = "rat";
  params["distortion"] = 1.0f;
  params["filter"] = 0.0f;
  params["volume"] = 1.0f;
  require(processor.configure(params, kHostRate, error), error);
  processor.reset();

  const int frames = static_cast<int>(kHostRate);
  for (int n = 0; n < frames; ++n) {
    const float x = 0.5f * std::sin(static_cast<float>(kTwoPi) * kTone * n / kHostRate);
    processor.process({x, x});
  }
  std::vector<float> captured(static_cast<std::size_t>(frames));
  for (int n = 0; n < frames; ++n) {
    const float phase = static_cast<float>(kTwoPi) * kTone * (frames + n) / kHostRate;
    captured[static_cast<std::size_t>(n)] = processor.process({0.5f * std::sin(phase),
                                                               0.5f * std::sin(phase)}).left;
  }

  const auto bin = [&captured](double frequency) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t n = 0; n < captured.size(); ++n) {
      const double phase = kTwoPi * frequency * static_cast<double>(n) / kHostRate;
      real += captured[n] * std::cos(phase);
      imaginary += captured[n] * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / static_cast<double>(captured.size());
  };

  const double fundamental = bin(kTone);
  require(fundamental > 0.05, "the tone must survive the pedal");

  // 3 kHz and 13 kHz are where the ninth and seventh harmonics land when they
  // fold. At four times oversampling they sat at about -40 dBc; eight times
  // puts them near -65 dBc, and the threshold here would catch a drop back.
  for (const double frequency : {3000.0, 13000.0}) {
    const double relativeDb = 20.0 * std::log10(bin(frequency) / fundamental);
    require(relativeDb < -55.0,
            "aliasing must stay far below the signal; at " + std::to_string(frequency)
              + " Hz it reached " + std::to_string(relativeDb) + " dBc");
  }

  // Even harmonics are a second, independent check that the diode pair is
  // matched: a symmetric clipper produces odd harmonics only.
  for (const double frequency : {10000.0, 20000.0}) {
    const double relativeDb = 20.0 * std::log10(bin(frequency) / fundamental);
    require(relativeDb < -70.0,
            "a symmetric clipper must not produce even harmonics; at "
              + std::to_string(frequency) + " Hz it reached " + std::to_string(relativeDb) + " dBc");
  }
}

void verifyProcessorRunsAndRejectsBadInput()
{
  ardor::RatProcessor processor;
  std::string error;
  nlohmann::json params;
  params["mode"] = "rat";
  params["distortion"] = 0.8f;
  params["filter"] = 0.4f;
  params["volume"] = 0.6f;
  require(processor.configure(params, 48000.0f, error), error);
  processor.reset();

  double peak = 0.0;
  for (int n = 0; n < 48000; ++n) {
    const float x = 0.3f * std::sin(static_cast<float>(kTwoPi) * 220.0f * n / 48000.0f);
    const auto out = processor.process({x, x});
    require(std::isfinite(out.left) && std::isfinite(out.right), "output must stay finite");
    require(out.left == out.right, "a mono pedal must return the same signal on both channels");
    if (n > 24000) peak = std::max(peak, static_cast<double>(std::fabs(out.left)));
  }
  require(peak > 0.05, "a driven RAT must produce output; measured " + std::to_string(peak));

  require(processor.setParameterTarget("distortion", 0.2f), "distortion must be settable");
  require(processor.setParameterTarget("filter", 0.9f), "filter must be settable");
  require(processor.setParameterTarget("volume", 1.0f), "volume must be settable");
  require(!processor.setParameterTarget("nonsense", 0.5f), "unknown parameters must be rejected");
  require(!processor.setParameterTarget("distortion", std::nanf("")),
          "a value that is not a number must be rejected");

  ardor::RatProcessor rejects;
  require(!rejects.configure(params, 0.0f, error), "a non-positive sample rate must be rejected");
  require(!error.empty(), "rejection must explain itself");
  params["mode"] = "big_cheese";
  require(!rejects.configure(params, 48000.0f, error), "an unknown mode must be rejected");
}

void verifyNetlistValidation()
{
  require(ardor::ratNetlistValid(ardor::ratNetlist()), "the shipped netlist must be realizable");
  auto broken = ardor::ratNetlist();
  broken.c7Farads = 0.0;
  require(!ardor::ratNetlistValid(broken), "a zero capacitor must be rejected");
  broken = ardor::ratNetlist();
  broken.r8Ohms = -1.0;
  require(!ardor::ratNetlistValid(broken), "a negative resistor must be rejected");
}

} // namespace

int main()
{
  verifyNetlistValidation();
  verifyMinimumDistortionIsUnity();
  verifyGainStageMatchesTheory();
  verifyGainRisesWithFrequency();
  verifyOpampSlewLimits();
  verifyDiodesClipSymmetrically();
  verifyFilterCorners();
  verifyOversamplingSuppressesAliasing();
  verifyProcessorRunsAndRejectsBadInput();
  std::printf("rat smoke passed\n");
  return 0;
}
