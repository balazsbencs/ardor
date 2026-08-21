#include "tape/TapeHysteresis.h"
#include "tape/TapeProcessor.h"
#include "tape/TapeTransport.h"

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

constexpr float kOversampledRate = 384000.0f;
constexpr float kTwoPi = 6.28318530718f;

// Drives the core with a triangular field sweep and records the magnetisation
// at every step, which traces the B-H loop directly.
std::vector<float> sweepLoop(float peakField, std::size_t stepsPerRamp)
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  std::vector<float> trace;
  trace.reserve(stepsPerRamp * 4);
  const auto ramp = [&](float from, float to) {
    for (std::size_t n = 0; n < stepsPerRamp; ++n) {
      const float t = static_cast<float>(n) / static_cast<float>(stepsPerRamp - 1);
      trace.push_back(core.process(from + (to - from) * t));
    }
  };
  // Two full cycles: the first magnetises from zero, the second is the loop.
  ramp(0.0f, peakField);
  ramp(peakField, -peakField);
  ramp(-peakField, peakField);
  ramp(peakField, -peakField);
  return trace;
}

void testLoopOpens()
{
  constexpr std::size_t kSteps = 2000;
  const auto trace = sweepLoop(4.0e4f, kSteps);
  // The third ramp climbs from -peak to +peak and the fourth falls back. At the
  // same field the two branches must differ: that difference IS hysteresis.
  // The midpoint of each ramp is field zero on both.
  const float ascending = trace[2 * kSteps + kSteps / 2];
  const float descending = trace[3 * kSteps + kSteps / 2];
  require(std::fabs(ascending - descending) > 0.02f,
          "the hysteresis loop must open: the two branches differ at the same field");
  require(ascending < descending,
          "the ascending branch must sit below the descending one");
}

void testLoopCloses()
{
  constexpr std::size_t kSteps = 2000;
  const auto trace = sweepLoop(4.0e4f, kSteps);
  // The second and fourth ramps are the same gesture one cycle apart. A solver
  // that drifts ends them in different places.
  const float firstCycleEnd = trace[2 * kSteps - 1];
  const float secondCycleEnd = trace[4 * kSteps - 1];
  require(std::fabs(firstCycleEnd - secondCycleEnd) < 0.01f,
          "the loop must close: the solver must not drift across cycles");
}

// Runs a sine at the oversampled rate and returns the magnitude of one
// harmonic, by direct correlation rather than an FFT.
double harmonicMagnitude(float fieldAmplitude, int harmonic)
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  constexpr float kFundamental = 1000.0f;
  const std::size_t settle = static_cast<std::size_t>(kOversampledRate / kFundamental) * 8;
  const std::size_t measure = static_cast<std::size_t>(kOversampledRate / kFundamental) * 64;

  for (std::size_t n = 0; n < settle; ++n) {
    const float t = static_cast<float>(n) / kOversampledRate;
    core.process(fieldAmplitude * std::sin(kTwoPi * kFundamental * t));
  }

  double real = 0.0;
  double imaginary = 0.0;
  for (std::size_t n = 0; n < measure; ++n) {
    const float t = static_cast<float>(settle + n) / kOversampledRate;
    const float out = core.process(fieldAmplitude * std::sin(kTwoPi * kFundamental * t));
    const double phase = static_cast<double>(kTwoPi) * kFundamental * harmonic * t;
    real += out * std::cos(phase);
    imaginary += out * std::sin(phase);
  }
  return 2.0 * std::sqrt(real * real + imaginary * imaginary) / static_cast<double>(measure);
}

void testOddHarmonicsDominateAndGrow()
{
  const double third = harmonicMagnitude(3.0e4f, 3);
  const double second = harmonicMagnitude(3.0e4f, 2);
  std::printf("  third/second harmonic ratio: %.1f\n", third / second);
  require(third > second * 4.0,
          "tape is odd-symmetric: the third harmonic must dominate the second");

  const double quiet = harmonicMagnitude(1.0e4f, 3);
  const double loud = harmonicMagnitude(3.0e4f, 3);
  const double louder = harmonicMagnitude(6.0e4f, 3);
  require(quiet < loud && loud < louder,
          "the third harmonic must grow monotonically with drive");
}

void testBoundedUnderAbuse()
{
  ardor::TapeHysteresis core;
  core.configure(ardor::TapeHysteresis::defaultParameters(), kOversampledRate);
  core.reset();

  // A full-scale square is the worst case for an RK4 Jiles-Atherton solver:
  // dH/dt is enormous and reverses sign thousands of times a second. This is
  // the documented way these solvers diverge.
  for (std::size_t n = 0; n < 400000; ++n) {
    const float field = (n / 24 % 2 == 0) ? 1.0e6f : -1.0e6f;
    const float out = core.process(field);
    require(std::isfinite(out), "the solver must stay finite on a full-scale square");
    require(std::fabs(out) < 4.0f, "the solver must stay bounded on a full-scale square");
  }

  // A DC step and then silence must settle, not ring or creep.
  for (std::size_t n = 0; n < 100000; ++n) {
    require(std::isfinite(core.process(5.0e4f)), "DC must stay finite");
  }
  for (std::size_t n = 0; n < 100000; ++n) {
    require(std::isfinite(core.process(0.0f)), "silence must stay finite");
  }
}

void testLangevinTaylorSwitchover()
{
  // coth(x) - 1/x is the difference of two diverging terms, so in float it
  // loses every significant bit near zero — it returns exactly 0.0 at x=1e-4.
  // The implementation switches to a Taylor series below a threshold. Both
  // branches are checked against a double-precision reference, because
  // checking the Taylor branch against its own series would prove nothing.
  const auto referenceLangevin = [](double x) { return 1.0 / std::tanh(x) - 1.0 / x; };
  const auto referencePrime = [](double x) {
    const double s = std::sinh(x);
    return 1.0 / (x * x) - 1.0 / (s * s);
  };

  // Walk two decades either side of the seam, so both branches are exercised
  // and the switchover itself is crossed.
  double worstValue = 0.0;
  double worstSlope = 0.0;
  for (int i = 0; i <= 80; ++i) {
    const double x = 1.0e-3 * std::pow(10.0, 3.0 * static_cast<double>(i) / 80.0);
    const float value = ardor::langevin(static_cast<float>(x));
    const float slope = ardor::langevinPrime(static_cast<float>(x));
    require(std::isfinite(value) && std::isfinite(slope), "Langevin must stay finite");

    worstValue = std::max(worstValue,
                          std::fabs(value - referenceLangevin(x)) / std::fabs(referenceLangevin(x)));
    worstSlope = std::max(worstSlope,
                          std::fabs(slope - referencePrime(x)) / std::fabs(referencePrime(x)));
  }
  std::printf("  Langevin worst relative error: %.2e value, %.2e slope\n", worstValue, worstSlope);
  require(worstValue < 1.0e-4, "Langevin must track its double reference across the seam");
  require(worstSlope < 1.0e-4, "Langevin slope must track its double reference across the seam");

  // Pin the seam itself. The log-spaced loop above may step over its immediate
  // neighbourhood, and the seam is the one place a wrong threshold hides: each
  // branch must be accurate at the exact point where the other takes over.
  // Comparing the two sides against each other would only measure the
  // function's own slope, so both are compared against the reference instead.
  // Tolerance is 1e-4 on both sides, the same as the sweep above. The closed
  // form's error near the seam is not smooth: it varies across a band of about
  // one ulp of the intermediate 1/x, which at x=0.1 works out to roughly 3e-5
  // relative. Measured 5.7e-7 on the Taylor side and 2.5e-5 on the closed side.
  // Both are around -90 dB, which is far below anything the magnetics can
  // express, so the seam is inaudible by a wide margin.
  for (const float side : {0.999f, 1.001f}) {
    const double x = static_cast<double>(ardor::kLangevinTaylorLimit) * side;
    const double error = std::fabs(ardor::langevin(static_cast<float>(x)) - referenceLangevin(x))
                       / std::fabs(referenceLangevin(x));
    require(error < 1.0e-4, "both branches must be accurate at the seam");
  }

  // Well away from zero the closed form must still be right: L(x) tends to 1.
  require(std::fabs(ardor::langevin(100.0f) - 0.99f) < 0.02f, "Langevin must saturate at 1");
  require(std::isfinite(ardor::langevinPrime(200.0f)), "Langevin slope must not overflow to NaN");
}

constexpr float kHostRate = 48000.0f;

// Correlates a signal against a probe frequency and returns its magnitude.
//
// The Hann window is not optional here. A bare rectangular correlation is only
// exact when the window holds a whole number of cycles, and most probe
// frequencies do not: 20 Hz over a 1.024 s window is 20.48 cycles. On a
// distorted signal the DC and harmonic terms then leak into the fundamental
// bin, which read as an 8 dB low-frequency lift that the block did not have.
// Hann drops the first sidelobe to -31 dB and rolls off steeply after it.
// The factor of two undoes the window's coherent gain of 0.5.
double binMagnitude(const std::vector<float>& signal, float frequency, float rate)
{
  const auto count = static_cast<double>(signal.size());
  double real = 0.0;
  double imaginary = 0.0;
  for (std::size_t n = 0; n < signal.size(); ++n) {
    const double position = static_cast<double>(n);
    const double window = 0.5 - 0.5 * std::cos(kTwoPi * position / count);
    const double phase = kTwoPi * frequency * position / rate;
    real += signal[n] * window * std::cos(phase);
    imaginary += signal[n] * window * std::sin(phase);
  }
  return 4.0 * std::sqrt(real * real + imaginary * imaginary) / count;
}

ardor::TapeTransport makeTransport(float flutter, float hissDb)
{
  ardor::TapeTransport transport;
  transport.configure(kHostRate);
  transport.setFlutter(flutter);
  transport.setHissDb(hissDb);
  transport.reset();
  return transport;
}

void testHissOffIsExactSilence()
{
  auto transport = makeTransport(0.0f, ardor::TapeTransport::kHissOffDb);
  for (std::size_t n = 0; n < 48000; ++n) {
    const auto out = transport.process({0.0f, 0.0f});
    require(out.left == 0.0f && out.right == 0.0f,
            "hiss off must be a hard off, not a quiet generator");
  }
}

void testHissOnIsAudibleAndUncorrelated()
{
  auto transport = makeTransport(0.0f, -70.0f);

  double leftEnergy = 0.0;
  double rightEnergy = 0.0;
  double crossEnergy = 0.0;
  for (std::size_t n = 0; n < 192000; ++n) {
    const auto out = transport.process({0.0f, 0.0f});
    leftEnergy += static_cast<double>(out.left) * out.left;
    rightEnergy += static_cast<double>(out.right) * out.right;
    crossEnergy += static_cast<double>(out.left) * out.right;
  }
  require(leftEnergy > 0.0 && rightEnergy > 0.0, "hiss on must produce noise");

  // Tape noise is physically uncorrelated between channels, so the normalised
  // cross-correlation must sit near zero. A shared generator would give 1.
  const double correlation = crossEnergy / std::sqrt(leftEnergy * rightEnergy);
  std::printf("  hiss channel correlation: %.4f\n", correlation);
  require(std::fabs(correlation) < 0.05, "hiss must be independent per channel");
}

void testFlutterIsSharedAcrossChannels()
{
  auto transport = makeTransport(1.0f, ardor::TapeTransport::kHissOffDb);

  // One reel passes one capstan, so both channels must be modulated by the
  // same transport. Independent modulation would tear the stereo image apart.
  for (std::size_t n = 0; n < 192000; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.5f * std::sin(kTwoPi * 440.0f * t);
    const auto out = transport.process({in, in});
    require(out.left == out.right,
            "identical input must give identical output: one transport, both channels");
  }
}

void testFlutterFullScaleMovesPitch()
{
  auto transport = makeTransport(1.0f, ardor::TapeTransport::kHissOffDb);

  std::vector<float> out;
  out.reserve(192000);
  for (std::size_t n = 0; n < 192000; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.5f * std::sin(kTwoPi * 1000.0f * t);
    out.push_back(transport.process({in, in}).left);
  }
  const std::vector<float> steady(out.begin() + 4800, out.end());

  const double carrier = binMagnitude(steady, 1000.0f, kHostRate);
  const double sideband = binMagnitude(steady, 1000.7f, kHostRate);
  std::printf("  full-scale wow sideband: %.1f dBc\n", 20.0 * std::log10(sideband / carrier));
  require(sideband > carrier * 0.0005, "full-scale flutter must actually move the pitch");
}

void testTransportDelayIsExactlyItsNominal()
{
  // A delay line cannot read ahead of its write pointer, so the transport
  // cannot be delay-neutral — it needs headroom for the modulation to swing
  // both ways. What it can be is *predictable*: at zero flutter the delay must
  // be exactly kNominalDelay, so the block's dry path can subtract a known
  // constant. If it were anything else, Mix would comb.
  //
  // This also stands in for "flutter at zero adds no sidebands", and proves it
  // far more strongly. A match against the delayed input to -80 dB cannot
  // contain a sideband above -80 dB. The spectral version of this test was
  // dropped because a 1000.7 Hz probe sits 1.33 bins off a rectangular
  // window's centre, so it measured leakage rather than modulation.
  auto transport = makeTransport(0.0f, ardor::TapeTransport::kHissOffDb);

  std::vector<float> input;
  std::vector<float> output;
  for (std::size_t n = 0; n < 8192; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.4f * std::sin(kTwoPi * 220.0f * t) + 0.2f * std::sin(kTwoPi * 1310.0f * t);
    input.push_back(in);
    output.push_back(transport.process({in, in}).left);
  }
  double errorEnergy = 0.0;
  double signalEnergy = 0.0;
  for (std::size_t n = 2048; n < input.size(); ++n) {
    const double reference = input[n - ardor::TapeTransport::kNominalDelay];
    const double difference = output[n] - reference;
    errorEnergy += difference * difference;
    signalEnergy += reference * reference;
  }
  const double errorDb = 10.0 * std::log10(errorEnergy / signalEnergy);
  std::printf("  transport delay error: %.1f dB\n", errorDb);
  require(errorDb < -80.0, "at zero flutter the transport delay must equal its nominal exactly");
}

ardor::TapeProcessor makeTape(const nlohmann::json& overrides)
{
  nlohmann::json params;
  params["mode"] = "tape";
  params["drive"] = 0.0f;
  params["saturation"] = 0.5f;
  params["bias"] = 0.5f;
  params["speed"] = "15";
  params["head_bump"] = 0.5f;
  params["flutter"] = 0.0f;
  params["hiss_db"] = -120.0f;
  params["mix"] = 1.0f;
  params["output_db"] = 0.0f;
  for (const auto& item : overrides.items()) params[item.key()] = item.value();

  ardor::TapeProcessor tape;
  std::string error;
  if (!tape.configure(params, kHostRate, error)) throw std::runtime_error(error);
  tape.reset();
  return tape;
}

std::vector<float> renderTone(ardor::TapeProcessor& tape, float frequency, float amplitude,
                              std::size_t frames)
{
  std::vector<float> out;
  out.reserve(frames);
  for (std::size_t n = 0; n < frames; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = amplitude * std::sin(kTwoPi * frequency * t);
    out.push_back(tape.process({in, in}).left);
  }
  return out;
}

double rms(const std::vector<float>& signal, std::size_t skip)
{
  double sum = 0.0;
  for (std::size_t n = skip; n < signal.size(); ++n) sum += static_cast<double>(signal[n]) * signal[n];
  return std::sqrt(sum / static_cast<double>(signal.size() - skip));
}

void testOversamplingSuppressesAliases()
{
  // A 12 kHz tone at full drive throws harmonics at 24, 36 and 48 kHz and
  // beyond. Solved at 48 kHz they would fold straight back into the audio
  // band. This is the test that proves the 8x oversampling earns its cost.
  auto tape = makeTape({{"drive", 24.0f}, {"saturation", 1.0f}});
  const auto out = renderTone(tape, 12000.0f, 0.9f, 131072);
  const std::vector<float> steady(out.begin() + 8192, out.end());

  const double carrier = binMagnitude(steady, 12000.0f, kHostRate);
  require(carrier > 0.0, "the carrier must survive");

  // Every harmonic of 12 kHz folds to either 12 kHz or DC, so energy at an
  // unrelated frequency is an alias. Probe a spread of non-harmonic bins.
  double worst = 0.0;
  for (const float probe : {700.0f, 1900.0f, 3300.0f, 5100.0f, 7700.0f, 9300.0f, 15100.0f, 17300.0f}) {
    worst = std::max(worst, binMagnitude(steady, probe, kHostRate));
  }
  const double worstDbc = 20.0 * std::log10(worst / carrier);
  std::printf("  worst in-band alias: %.1f dBc\n", worstDbc);
  require(worstDbc < -70.0, "in-band aliases must stay under -70 dBc");
}

// Magnitude response in dB at one frequency, measured through the block.
double responseDb(ardor::TapeProcessor& tape, float frequency)
{
  const auto out = renderTone(tape, frequency, 0.05f, 65536);
  const std::vector<float> steady(out.begin() + 16384, out.end());
  return 20.0 * std::log10(binMagnitude(steady, frequency, kHostRate) / 0.05);
}

void testHeadBumpFollowsSpeed()
{
  // The bump has to be measured against the same machine with head_bump at
  // zero, at the same frequency. Comparing absolute responses would fold in
  // whatever gain and roll-off the rest of the block has, which is not what
  // this test is about.
  const auto bumpDb = [](const char* speed, float frequency) {
    auto shaped = makeTape({{"speed", speed}, {"head_bump", 1.0f}});
    auto reference = makeTape({{"speed", speed}, {"head_bump", 0.0f}});
    return responseDb(shaped, frequency) - responseDb(reference, frequency);
  };

  const double slowAt45 = bumpDb("15", 45.0f);
  const double slowAt90 = bumpDb("15", 90.0f);
  const double fastAt45 = bumpDb("30", 45.0f);
  const double fastAt90 = bumpDb("30", 90.0f);
  std::printf("  head bump: 15ips %.2f dB @45Hz / %.2f dB @90Hz, 30ips %.2f dB @45Hz / %.2f dB @90Hz\n",
              slowAt45, slowAt90, fastAt45, fastAt90);

  require(slowAt45 > 1.5, "15 ips must show a head bump near 45 Hz");
  require(fastAt90 > 0.8, "30 ips must show its smaller bump near 90 Hz");
  require(slowAt45 > fastAt45 + 1.0, "the 15 ips bump must be lower in frequency than the 30 ips one");
  require(fastAt90 > slowAt90, "the 30 ips bump must be higher in frequency than the 15 ips one");
  require(slowAt45 > fastAt90, "the slower speed's bump must be the larger one");

  // head_bump at zero must leave the low end flat, judged against the same
  // machine at 1 kHz where no bump filter reaches.
  auto flat = makeTape({{"speed", "15"}, {"head_bump", 0.0f}});
  const double flatTilt = responseDb(flat, 45.0f) - responseDb(flat, 1000.0f);
  std::printf("  head bump off, 45 Hz vs 1 kHz: %.2f dB\n", flatTilt);
  require(std::fabs(flatTilt) < 0.3, "head_bump at zero must leave the low end flat");
}

void testMixZeroIsTransparent()
{
  // This simultaneously proves the dry path is delay-matched. If the dry
  // signal were not delayed by the same amount the wet path costs, mix would
  // comb rather than blend.
  auto tape = makeTape({{"mix", 0.0f}, {"drive", 24.0f}});

  std::vector<float> input;
  std::vector<float> output;
  for (std::size_t n = 0; n < 32768; ++n) {
    const float t = static_cast<float>(n) / kHostRate;
    const float in = 0.4f * std::sin(kTwoPi * 220.0f * t) + 0.2f * std::sin(kTwoPi * 1310.0f * t);
    input.push_back(in);
    output.push_back(tape.process({in, in}).left);
  }

  const std::size_t latency = tape.latencyFrames();
  double errorEnergy = 0.0;
  double signalEnergy = 0.0;
  for (std::size_t n = 8192; n < input.size(); ++n) {
    const double reference = input[n - latency];
    const double difference = output[n] - reference;
    errorEnergy += difference * difference;
    signalEnergy += reference * reference;
  }
  const double errorDb = 10.0 * std::log10(errorEnergy / signalEnergy);
  std::printf("  mix=0 error: %.1f dB\n", errorDb);
  require(errorDb < -80.0, "mix at zero must reproduce the input within -80 dB");
}

void testDriveIsGainCompensated()
{
  // Drive must change character, not loudness. The compensation is calibrated
  // at configure() time by running a probe through a scratch copy of the
  // magnetics, so this checks that calibration.
  double quietest = 1.0e9;
  double loudest = -1.0e9;
  for (const float drive : {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f}) {
    auto tape = makeTape({{"drive", drive}});
    const auto out = renderTone(tape, 440.0f, 0.3f, 32768);
    const double level = 20.0 * std::log10(rms(out, 8192));
    quietest = std::min(quietest, level);
    loudest = std::max(loudest, level);
  }
  std::printf("  drive level swing: %.2f dB\n", loudest - quietest);
  require(loudest - quietest < 3.0, "drive must move the output level less than 3 dB");
}

void testResetReturnsToConstructedState()
{
  auto tape = makeTape({{"drive", 18.0f}});
  const auto first = renderTone(tape, 440.0f, 0.5f, 8192);
  tape.reset();
  const auto second = renderTone(tape, 440.0f, 0.5f, 8192);
  for (std::size_t n = 0; n < first.size(); ++n) {
    require(first[n] == second[n], "reset must return the block to its constructed state");
  }
}

void testRejectsBadConfiguration()
{
  ardor::TapeProcessor tape;
  std::string error;
  nlohmann::json params;
  params["mode"] = "tape";
  require(!tape.configure(params, 0.0f, error), "a zero sample rate must be rejected");
  require(!error.empty(), "a rejected configuration must say why");

  params["mode"] = "rat";
  require(!tape.configure(params, kHostRate, error), "the wrong mode must be rejected");

  params["mode"] = "tape";
  params["speed"] = "7.5";
  require(!tape.configure(params, kHostRate, error), "an unsupported tape speed must be rejected");

  params.erase("speed");
  require(tape.configure(params, kHostRate, error), "defaults must configure cleanly");
  require(!tape.setParameterTarget("speed", 30.0f), "speed must not be live-settable");
  require(tape.setParameterTarget("drive", 6.0f), "drive must be live-settable");
  require(!tape.setParameterTarget("nonsense", 1.0f), "unknown keys must be rejected");
  require(!tape.setParameterTarget("drive", std::nanf("")), "a non-finite value must be rejected");
}

void testToneControlsCanUpdateWhileProcessing()
{
  auto tape = makeTape({});
  for (int step = 0; step <= 32; ++step) {
    const float value = static_cast<float>(step) / 32.0f;
    require(tape.setParameterTarget("saturation", value), "saturation must be live-settable");
    require(tape.setParameterTarget("bias", 1.0f - value), "bias must be live-settable");
    require(tape.setParameterTarget("head_bump", value), "head bump must be live-settable");
    const auto out = tape.process({0.25f, -0.25f});
    require(std::isfinite(out.left) && std::isfinite(out.right),
            "live tape tone updates must keep the audio finite");
  }
}

} // namespace

int main()
{
  try {
    testLoopOpens();
    testLoopCloses();
    testOddHarmonicsDominateAndGrow();
    testBoundedUnderAbuse();
    testLangevinTaylorSwitchover();
    testHissOffIsExactSilence();
    testHissOnIsAudibleAndUncorrelated();
    testFlutterIsSharedAcrossChannels();
    testFlutterFullScaleMovesPitch();
    testTransportDelayIsExactlyItsNominal();
    testOversamplingSuppressesAliases();
    testHeadBumpFollowsSpeed();
    testMixZeroIsTransparent();
    testDriveIsGainCompensated();
    testResetReturnsToConstructedState();
    testRejectsBadConfiguration();
    testToneControlsCanUpdateWhileProcessing();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "tape smoke failed: %s\n", error.what());
    return 1;
  }
  std::printf("tape smoke passed\n");
  return 0;
}
