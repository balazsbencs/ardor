// Objective quality checks for the filter-type modulation effects: Filter,
// Ladder Sweep, Formant and Quadrature. Each check pins one measured defect
// from the filter review.

#include "mod_effect_test_support.h"

#include <complex>
#include <cstdio>
#include <utility>

namespace {

using namespace mod_test;

double magnitude(const std::vector<float>& x, size_t start, double hz)
{
  constexpr size_t kN = 32768;
  std::complex<double> acc = 0.0;
  for (size_t i = 0; i < kN; ++i) {
    const double window = 0.5 - 0.5 * std::cos(kTwoPi * i / (kN - 1));
    acc += window * static_cast<double>(x[start + i]) * std::polar(1.0, -kTwoPi * hz * i / kSampleRate);
  }
  return std::abs(acc);
}

// Steady-state response of an effect at one frequency, dB re input.
double responseDb(const nlohmann::json& params, double hz, float amplitude = 0.05f)
{
  const auto out = render(params, sine(hz, amplitude, 48000));
  double sum = 0.0;
  for (size_t i = 24000; i < 48000; ++i) sum += out.left[i] * out.left[i];
  return db(std::sqrt(sum / 24000.0) / (amplitude / std::sqrt(2.0)));
}

// --- Quadrature -----------------------------------------------------------------

// The Hilbert pair had its sections' sign flipped and the delay on the wrong
// path, so the "single-sideband" shift left the unwanted sideband within
// 0.1 to 4.7 dB of the wanted one below 2 kHz.
void verifyFrequencyShifterRejectsTheOtherSideband()
{
  constexpr double kShift = 20.0;
  for (const double hz : {60.0, 150.0, 500.0, 2000.0, 8000.0, 15000.0}) {
    auto params = defaults("quadrature");
    params["p2"] = 0.6f;                                                // Shift +
    params["p1"] = 0.0f;                                                // no dry
    params["depth"] = 0.0f;                                             // no feedback
    params["speed"] = static_cast<float>(std::cbrt((kShift - 0.1) / 999.9)); // 20 Hz
    const auto out = render(params, sine(hz, 0.3f, 2 * 48000));
    const double rejection = db(magnitude(out.left, 48000, hz + kShift) /
                                magnitude(out.left, 48000, hz - kShift));
    require(rejection > 35.0, "frequency shifter must reject the other sideband at " + fmt(hz) +
                                  " Hz, got " + fmt(rejection) + " dB");
  }
}

// Depth did nothing in AM, Shift + and Shift -.
void verifyQuadratureDepthWorksInEveryType()
{
  for (const float type : {0.1f, 0.6f, 0.9f}) {
    auto shallow = defaults("quadrature");
    shallow["p2"] = type;
    shallow["depth"] = 0.0f;
    auto deep = shallow;
    deep["depth"] = 1.0f;
    require(maxDifference(shallow, deep) > 1e-3, "quadrature Depth must work in type " + fmt(type));
  }
  // AM at zero depth leaves the note alone.
  auto am = defaults("quadrature");
  am["p2"] = 0.1f;
  am["depth"] = 0.0f;
  const auto input = sine(440.0, 0.3f, 48000);
  const auto out = render(am, input);
  double diff = 0.0;
  for (size_t i = 0; i < input.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(out.left[i] - input[i])));
  require(diff < 1e-3, "quadrature AM at zero depth must pass the note unchanged, diff " + fmt(diff));
}

// --- Filter ---------------------------------------------------------------------

nlohmann::json staticFilter(float type, float tone, float resonance)
{
  auto params = defaults("filter");
  params["depth"] = 0.0f;
  params["p3"] = type;   // LP / BP / HP / Notch
  params["tone"] = tone;
  params["p1"] = resonance;
  return params;
}

// Tone used to pick the filter type as well as the frequency, which locked the
// band-pass to 4.5-7.6 kHz and the high-pass above 6.9 kHz. Every type must now
// reach every frequency, and Tone must set the frequency alone.
void verifyFilterTypeAndFrequencyAreIndependent()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "filter");
  require(descriptor != nullptr && descriptor->params.size() >= 8 && descriptor->params[7].key == "p3",
          "filter must have a Type control");
  // Tone 0.4 -> 80 * 150^0.4 = 594 Hz.
  const double centre = 80.0 * std::pow(150.0, 0.4);
  const auto bandPass = staticFilter(1.0f / 3.0f, 0.4f, 0.3f);
  require(responseDb(bandPass, centre) > responseDb(bandPass, centre * 4.0) + 10.0 &&
              responseDb(bandPass, centre) > responseDb(bandPass, centre / 4.0) + 10.0,
          "band-pass must peak at the Tone frequency");
  const auto highPass = staticFilter(2.0f / 3.0f, 0.4f, 0.1f);
  require(responseDb(highPass, centre * 4.0) > -1.0 && responseDb(highPass, centre / 4.0) < -15.0,
          "high-pass must cut below the Tone frequency and pass above it");
  const auto lowPass = staticFilter(0.0f, 0.4f, 0.1f);
  require(responseDb(lowPass, centre / 4.0) > -1.0 && responseDb(lowPass, centre * 4.0) < -15.0,
          "low-pass must pass below the Tone frequency and cut above it");
  const auto notch = staticFilter(1.0f, 0.4f, 0.3f);
  require(responseDb(notch, centre) < -15.0 && responseDb(notch, centre * 4.0) > -1.0,
          "notch must cut the Tone frequency");

  const double level = levelDb(defaults("filter"));
  require(std::fabs(level) < 4.0, "filter defaults must stay near bypass level, got " + fmt(level));
}

// A soft clip on the output distorted every signal: -36 dBc of third harmonic
// on a -6 dBFS tone through a gentle low-pass.
void verifyFilterIsClean()
{
  const auto out = render(staticFilter(0.0f, 0.7f, 0.0f), sine(1000.0, 0.5f, 2 * 48000));
  const double h3 = db(magnitude(out.left, 48000, 3000.0) / magnitude(out.left, 48000, 1000.0));
  require(h3 < -80.0, "filter must not distort a clean signal, H3 " + fmt(h3) + " dBc");
}

// Q 20 put a +24 dB peak into the output clip.
void verifyFilterResonanceIsContained()
{
  for (const float type : {0.0f, 1.0f / 3.0f, 2.0f / 3.0f}) {
    const auto params = staticFilter(type, 0.4f, 1.0f);
    double peak = -1e9;
    for (double hz = 200.0; hz < 3000.0; hz *= 1.01) peak = std::max(peak, responseDb(params, hz));
    require(peak < 12.5, "filter resonance peak must stay within +12 dB, type " + fmt(type) + " peaks " +
                             fmt(peak));
  }
}

// --- Ladder Sweep ---------------------------------------------------------------

// The feedback used the previous sample's output, which made the loop unstable
// at high cutoff: at 12 kHz it rang on at -16 dBFS with Resonance at half.
void verifyLadderIsStableBelowSelfOscillation()
{
  for (const float tone : {0.3f, 0.6f, 0.8f, 1.0f}) {
    for (const float resonance : {0.5f, 0.9f}) {
      auto params = defaults("ladder_sweep");
      params["depth"] = 0.0f;
      params["tone"] = tone;
      params["p1"] = resonance;
      std::vector<float> input(2 * 48000, 0.0f);
      input[100] = 0.5f;
      const auto out = render(params, input);
      double sum = 0.0;
      for (size_t i = 48000; i < input.size(); ++i) sum += out.left[i] * out.left[i];
      const double ring = 10.0 * std::log10(sum / 48000.0 + 1e-30);
      require(ring < -90.0, "ladder must decay below self-oscillation: tone " + fmt(tone) + " resonance " +
                                fmt(resonance) + " still at " + fmt(ring) + " dBFS after 1 s");
    }
  }
  auto oscillating = defaults("ladder_sweep");
  oscillating["depth"] = 0.0f;
  oscillating["tone"] = 0.5f;
  oscillating["p1"] = 1.0f;
  std::vector<float> input(2 * 48000, 0.0f);
  input[100] = 0.5f;
  const auto out = render(oscillating, input);
  double sum = 0.0;
  for (size_t i = 48000; i < input.size(); ++i) sum += out.left[i] * out.left[i];
  require(10.0 * std::log10(sum / 48000.0 + 1e-30) > -40.0, "ladder must self-oscillate at full Resonance");
}

// Resonance raised the level by 13 dB and Drive swept it across 20 dB.
void verifyLadderKeepsItsLevel()
{
  const auto levelAt = [](float resonance, float drive) {
    auto params = defaults("ladder_sweep");
    params["depth"] = 0.0f;
    params["tone"] = 0.6f;
    params["p1"] = resonance;
    params["level"] = drive;
    return levelDb(params);
  };
  const double reference = levelAt(0.0f, 0.25f);
  for (const float resonance : {0.5f, 0.9f}) {
    const double change = levelAt(resonance, 0.25f) - reference;
    require(std::fabs(change) < 4.0, "ladder Resonance " + fmt(resonance) + " must keep the level within 4 dB, got " +
                                         fmt(change));
  }
  for (const float drive : {0.0f, 0.5f, 1.0f}) {
    const double change = levelAt(0.0f, drive) - reference;
    require(std::fabs(change) < 3.0, "ladder Drive " + fmt(drive) + " must keep the level within 3 dB, got " +
                                         fmt(change));
  }
}

// --- Formant --------------------------------------------------------------------

// Five unity band-passes summed and divided by five sat at -17 dB, and all five
// formants had the same level.
void verifyFormantLevelAndSpectrum()
{
  for (const float resonance : {0.0f, 0.5f, 1.0f}) {
    auto params = defaults("formant");
    params["p1"] = resonance;
    const double level = levelDb(params);
    require(std::fabs(level) < 4.0, "formant at resonance " + fmt(resonance) +
                                        " must stay near bypass level, got " + fmt(level));
  }
  auto params = defaults("formant");
  params["depth"] = 0.0f;
  params["p1"] = 0.5f;
  const double f1 = responseDb(params, 800.0);
  const double f4 = responseDb(params, 3500.0);
  const double f5 = responseDb(params, 4950.0);
  require(f4 < f1 - 10.0 && f5 < f1 - 10.0, "formant upper formants must sit well below F1: F1 " + fmt(f1) +
                                               ", F4 " + fmt(f4) + ", F5 " + fmt(f5));
}

// --- Tone loudness --------------------------------------------------------------

void verifyFilterEffectsToneKeepsLoudness()
{
  for (const char* mode : {"formant", "quadrature"}) {
    auto params = defaults(mode);
    params["tone"] = 0.5f;
    const double flat = levelDb(params);
    for (const float tone : {0.0f, 1.0f}) {
      params["tone"] = tone;
      const double change = levelDb(params) - flat;
      require(std::fabs(change) < 1.5, std::string(mode) + " Tone " + fmt(tone) +
                                           " must keep loudness within 1.5 dB, got " + fmt(change));
    }
  }
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"frequency shifter", verifyFrequencyShifterRejectsTheOtherSideband},
    {"quadrature depth", verifyQuadratureDepthWorksInEveryType},
    {"filter type", verifyFilterTypeAndFrequencyAreIndependent},
    {"filter clean", verifyFilterIsClean},
    {"filter resonance", verifyFilterResonanceIsContained},
    {"ladder stability", verifyLadderIsStableBelowSelfOscillation},
    {"ladder level", verifyLadderKeepsItsLevel},
    {"formant level", verifyFormantLevelAndSpectrum},
    {"tone loudness", verifyFilterEffectsToneKeepsLoudness},
  };
  int failures = 0;
  for (const auto& [name, check] : checks) {
    try {
      check();
    } catch (const std::exception& error) {
      std::fprintf(stderr, "FAIL %s: %s\n", name, error.what());
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
