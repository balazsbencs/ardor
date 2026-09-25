// Objective quality checks for the hosted delays. Each check pins one measured
// defect from the delay review (D1-D6, D8 in docs/dsp-effects-review-*.md).

#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr float kRate = 48000.0f;

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

std::string fmt(double value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.3f", value);
  return buffer;
}

double db(double value) { return 20.0 * std::log10(std::max(value, 1e-12)); }

nlohmann::json defaults(const std::string& mode)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("delay", mode);
  require(descriptor != nullptr, mode + " descriptor exists");
  return ardor::defaultDaisyFxParams(*descriptor);
}

// Normalised Time for a physical delay on the 0.06..2.5 s, t^3 range.
float timeFor(double seconds) { return static_cast<float>(std::cbrt((seconds - 0.06) / 2.44)); }

struct Render {
  std::vector<float> left;
  std::vector<float> right;
};

Render render(const nlohmann::json& params, const std::vector<float>& input)
{
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("delay", params, kRate, error), error);
  Render out;
  for (const float x : input) {
    const auto y = processor.process({x, x});
    out.left.push_back(y.left);
    out.right.push_back(y.right);
  }
  return out;
}

std::vector<float> sine(double hz, float amplitude, size_t frames, size_t end = SIZE_MAX)
{
  std::vector<float> out(frames, 0.0f);
  for (size_t i = 0; i < frames && i < end; ++i) {
    out[i] = amplitude * static_cast<float>(std::sin(kTwoPi * hz * i / kRate));
  }
  return out;
}

double rmsDb(const std::vector<float>& x, size_t begin, size_t end)
{
  double sum = 0.0;
  for (size_t i = begin; i < end; ++i) sum += static_cast<double>(x[i]) * x[i];
  return 10.0 * std::log10(std::max(sum / (end - begin), 1e-30));
}

double magnitude(const std::vector<float>& x, size_t start, double hz)
{
  constexpr size_t kN = 32768;
  std::complex<double> acc = 0.0;
  for (size_t i = 0; i < kN; ++i) {
    const double window = 0.5 - 0.5 * std::cos(kTwoPi * i / (kN - 1));
    acc += window * static_cast<double>(x[start + i]) * std::polar(1.0, -kTwoPi * hz * i / kRate);
  }
  return std::abs(acc);
}

// A plucked-string phrase with a 1/n harmonic series, peaking near -6 dBFS.
const std::vector<float>& guitarPhrase()
{
  static const std::vector<float> phrase = [] {
    constexpr double kNotes[] = {82.4, 110.0, 146.8, 196.0, 246.9, 329.6};
    std::vector<float> out;
    for (const double f0 : kNotes) {
      for (int i = 0; i < 24000; ++i) {
        const double t = i / static_cast<double>(kRate);
        double sample = 0.0;
        for (int h = 1; h <= 20 && f0 * h < 12000.0; ++h) {
          sample += std::sin(kTwoPi * f0 * h * t) * std::exp(-t * (2.0 + 0.6 * h)) / h;
        }
        out.push_back(static_cast<float>(0.3 * sample));
      }
    }
    return out;
  }();
  return phrase;
}

// --- D1: dry at unity ------------------------------------------------------------

// The host blended dry and wet with a linear crossfade, so engaging a delay at
// the default 25 % Mix turned the dry note down by 2.5 dB (6 dB at 50 %).
void verifyDryStaysAtUnity()
{
  const auto input = sine(1000.0, 0.3f, 12000);
  for (const float mix : {0.25f, 0.5f}) {
    auto params = defaults("digital");
    params["mix"] = mix;
    params["time"] = timeFor(0.5);
    const auto out = render(params, input);
    // Before the first repeat arrives, the output is the dry note alone.
    const double dry = rmsDb(out.left, 2400, 12000) - rmsDb(input, 2400, 12000);
    require(std::fabs(dry) < 0.05, "delay dry must stay at unity at mix " + fmt(mix) + ", got " + fmt(dry));
  }
  // The wet level at a given Mix is unchanged, so saved presets keep their
  // balance of repeats; only the dry note gets its level back.
  auto params = defaults("digital");
  params["mix"] = 0.25f;
  params["repeats"] = 0.0f;
  params["time"] = timeFor(0.1);
  const auto burst = sine(1000.0, 0.3f, 24000, 2400);
  const auto out = render(params, burst);
  double wet = -300.0;
  for (size_t a = 4800; a + 1200 < out.left.size(); a += 600) {
    wet = std::max(wet, rmsDb(out.left, a, a + 1200));
  }
  // After the burst ends only the repeat remains: 0.25 of the input.
  const double expected = rmsDb(burst, 0, 2400) + db(0.25);
  require(std::fabs(wet - expected) < 0.5, "delay wet level at 25 % Mix must stay at 0.25, got " +
                                               fmt(wet - expected) + " dB off");
}

// --- D2: Grit without loop gain ----------------------------------------------------

// Grit blended in a saturator whose small-signal gain grew to 16x. At Repeats
// 0.35 and full Grit the repeats never decayed.
void verifyGritDoesNotSustainRepeats()
{
  for (const char* mode : {"digital", "tape"}) {
    for (const float grit : {0.5f, 1.0f}) {
      auto params = defaults(mode);
      params["mix"] = 1.0f;
      params["repeats"] = 0.35f;
      params["grit"] = grit;
      params["time"] = timeFor(0.3);
      const auto input = sine(440.0, 0.3f, 8 * 48000, 4800);
      const auto out = render(params, input);
      const double tail = rmsDb(out.left, 6 * 48000, 7 * 48000) - rmsDb(input, 0, 4800);
      require(tail < -60.0, std::string(mode) + " at Grit " + fmt(grit) +
                                " must let repeats decay, 6 s later still at " + fmt(tail) + " dB");
    }
  }
}

// --- D3: Filter Delay --------------------------------------------------------------

void verifyFilterDelayIsCleanAndContained()
{
  auto params = defaults("filter");
  params["mix"] = 1.0f;
  params["repeats"] = 0.0f;
  params["time"] = timeFor(0.1);
  params["filter"] = 0.0f; // lowest resonance
  const auto out = render(params, sine(1000.0, 0.6f, 3 * 48000));
  const double h3 = db(magnitude(out.left, 48000, 3000.0) / magnitude(out.left, 48000, 1000.0));
  require(h3 < -80.0, "filter delay must not distort, H3 " + fmt(h3) + " dBc");

  for (const float grit : {0.0f, 0.5f, 1.0f}) { // LP, BP, HP
    auto resonant = defaults("filter");
    resonant["mix"] = 1.0f;
    resonant["repeats"] = 0.0f;
    resonant["time"] = timeFor(0.1);
    resonant["filter"] = 1.0f; // Q 15
    resonant["grit"] = grit;
    double peak = -300.0;
    for (double hz = 100.0; hz < 8000.0; hz *= 1.03) {
      const auto r = render(resonant, sine(hz, 0.05f, 24000));
      peak = std::max(peak, rmsDb(r.left, 12000, 24000) - rmsDb(sine(hz, 0.05f, 24000), 12000, 24000));
    }
    require(peak < 12.5, "filter delay resonance must stay within +12 dB, type " + fmt(grit) + " peaks " +
                             fmt(peak));
  }
}

// --- D4, D5: Tone ---------------------------------------------------------------

double firstRepeatDb(nlohmann::json params)
{
  params["mix"] = 1.0f;
  params["repeats"] = 0.0f;
  const auto& input = guitarPhrase();
  const auto out = render(params, input);
  return rmsDb(out.left, 24000, out.left.size());
}

// The loop-safe tilt set the first repeat's level too: -9.4 dB at Tone 1. In
// Tape, Tone did nothing to the first repeat at all.
void verifyToneKeepsTheFirstRepeat()
{
  for (const char* mode : {"digital", "tape", "dual", "duck", "pattern", "trem"}) {
    auto params = defaults(mode);
    if (std::string(mode) == "trem") params["mod_dep"] = 0.0f;
    params["filter"] = 0.5f;
    const double flat = firstRepeatDb(params);
    for (const float tone : {0.0f, 1.0f}) {
      params["filter"] = tone;
      const double change = firstRepeatDb(params) - flat;
      require(std::fabs(change) < 1.5, std::string(mode) + " Tone " + fmt(tone) +
                                           " must keep the first repeat's loudness, got " + fmt(change));
    }
  }
  auto dark = defaults("tape");
  dark["filter"] = 0.0f;
  auto bright = dark;
  bright["filter"] = 1.0f;
  const auto& input = guitarPhrase();
  dark["mix"] = bright["mix"] = 1.0f;
  dark["repeats"] = bright["repeats"] = 0.0f;
  const auto a = render(dark, input), b = render(bright, input);
  double diff = 0.0;
  for (size_t i = 0; i < a.left.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(a.left[i] - b.left[i])));
  require(diff > 1e-3, "tape Tone must shape the first repeat");
}

// Tape Grit coloured only the feedback, so the first repeat never saturated.
void verifyTapeGritColoursTheFirstRepeat()
{
  const auto thirdAt = [](float grit) {
    auto params = defaults("tape");
    params["mix"] = 1.0f;
    params["repeats"] = 0.0f;
    params["grit"] = grit;
    params["time"] = timeFor(0.1);
    const auto out = render(params, sine(500.0, 0.5f, 3 * 48000));
    return db(magnitude(out.left, 48000, 1500.0) / magnitude(out.left, 48000, 500.0));
  };
  const double clean = thirdAt(0.0f);
  const double driven = thirdAt(1.0f);
  require(driven > clean + 20.0, "tape Grit must saturate the first repeat, H3 " + fmt(clean) + " -> " +
                                     fmt(driven) + " dBc");
}

// --- D6: Dual modulation ------------------------------------------------------------

double modulationCents(const nlohmann::json& base, double seconds)
{
  auto params = base;
  params["mix"] = 1.0f;
  params["repeats"] = 0.0f;
  params["time"] = timeFor(seconds);
  params["mod_dep"] = 1.0f;
  params["mod_spd"] = static_cast<float>(std::sqrt((1.0 - 0.05) / 9.95));
  const auto out = render(params, sine(1000.0, 0.3f, 6 * 48000));
  std::vector<double> crossings;
  for (size_t i = 2 * 48000; i < out.left.size(); ++i) {
    if (out.left[i - 1] < 0.0f && out.left[i] >= 0.0f) {
      crossings.push_back(static_cast<double>(i - 1) + out.left[i - 1] / (out.left[i - 1] - out.left[i]));
    }
  }
  double lo = 1e9, hi = -1e9;
  for (size_t k = 8; k < crossings.size(); k += 8) {
    const double hz = 8.0 * kRate / (crossings[k] - crossings[k - 8]);
    lo = std::min(lo, hz);
    hi = std::max(hi, hz);
  }
  return 1200.0 * std::log2(hi / lo);
}

// Dual's depth was a fraction of the delay time: 11 cents at 0.1 s, 109 at 1 s.
void verifyDualModulationIsAbsolute()
{
  const auto params = defaults("dual");
  const double short_ = modulationCents(params, 0.1);
  const double long_ = modulationCents(params, 1.0);
  require(long_ < short_ * 1.3 && long_ > short_ * 0.7,
          "dual modulation must not grow with time: " + fmt(short_) + " and " + fmt(long_) + " cents");
}

// --- D8: clean at zero Grit ---------------------------------------------------------

// Tape and Bucket Brigade saturated even at Grit 0: -44 and -32 dBc of third
// harmonic on a 0.6 tone.
void verifyCleanAtZeroGrit()
{
  for (const char* mode : {"tape", "dbucket"}) {
    auto params = defaults(mode);
    params["mix"] = 1.0f;
    params["repeats"] = 0.0f;
    params["grit"] = 0.0f;
    params["time"] = timeFor(0.1);
    const auto out = render(params, sine(1000.0, 0.6f, 3 * 48000));
    const double h3 = db(magnitude(out.left, 48000, 3000.0) / magnitude(out.left, 48000, 1000.0));
    require(h3 < -60.0, std::string(mode) + " at Grit 0 must be clean, H3 " + fmt(h3) + " dBc");
  }
}

// --- D7: Width ----------------------------------------------------------------

// The right head ran 31-150 samples behind the left in eight modes, which
// combed the repeats by 47-75 dB when a mono rig summed the channels. Width
// (index 7, after the original seven) sets the stereo image: 1 is the old
// image and the default, 0 is exactly mono.
void verifyWidthGivesAMonoSafeImage()
{
  constexpr const char* kModes[] = {"digital", "tape", "dual", "filter", "lofi",
                                    "dbucket", "duck", "pattern", "swell", "trem"};
  for (const char* mode : kModes) {
    const auto* descriptor = ardor::findDaisyFxDescriptor("delay", mode);
    require(descriptor->params.size() == 8 && descriptor->params[7].key == "width",
            std::string(mode) + " must have Width at index 7");

    // A preset saved before Width existed keeps its sound.
    auto legacy = defaults(mode);
    legacy.erase("width");
    auto wide = defaults(mode);
    wide["width"] = 1.0f;
    const auto& input = guitarPhrase();
    const auto a = render(legacy, input), b = render(wide, input);
    double diff = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i) {
      diff = std::max({diff, static_cast<double>(std::fabs(a.left[i] - b.left[i])),
                       static_cast<double>(std::fabs(a.right[i] - b.right[i]))});
    }
    require(diff == 0.0, std::string(mode) + " must treat a missing Width as 1");

    // Width 0: both channels carry the same repeats.
    auto mono = defaults(mode);
    mono["width"] = 0.0f;
    mono["mix"] = 1.0f;
    const auto m = render(mono, input);
    double spread = 0.0;
    for (size_t i = 0; i < m.left.size(); ++i) {
      spread = std::max(spread, static_cast<double>(std::fabs(m.left[i] - m.right[i])));
    }
    require(spread < 1e-6, std::string(mode) + " at Width 0 must give identical channels, differ by " +
                               fmt(spread));
  }

  // And the mono sum of an impulse's repeat is flat: no comb.
  for (const char* mode : {"digital", "tape", "dual", "filter", "lofi", "dbucket", "duck"}) {
    auto params = defaults(mode);
    params["width"] = 0.0f;
    params["mix"] = 1.0f;
    params["repeats"] = 0.0f;
    params["time"] = timeFor(0.3);
    params["filter"] = std::string(mode) == "filter" ? 0.0f : 0.5f;
    std::vector<float> impulse(48000, 0.0f);
    impulse[10] = 1.0f;
    const auto out = render(params, impulse);
    std::vector<float> sum(out.left.size());
    for (size_t i = 0; i < sum.size(); ++i) sum[i] = 0.5f * (out.left[i] + out.right[i]);
    std::vector<float> reference(out.left);
    double worst = 0.0;
    for (double hz = 100.0; hz < 4000.0; hz *= 1.02) {
      const double change = db(magnitude(sum, 0, hz) / magnitude(reference, 0, hz));
      worst = std::max(worst, std::fabs(change));
    }
    require(worst < 1.0, std::string(mode) + " at Width 0 must sum to mono without a comb, worst " +
                             fmt(worst) + " dB");
  }
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"D1 dry at unity", verifyDryStaysAtUnity},
    {"D2 grit decay", verifyGritDoesNotSustainRepeats},
    {"D3 filter delay", verifyFilterDelayIsCleanAndContained},
    {"D4/D5 tone", verifyToneKeepsTheFirstRepeat},
    {"D5 tape grit", verifyTapeGritColoursTheFirstRepeat},
    {"D6 dual modulation", verifyDualModulationIsAbsolute},
    {"D8 clean at zero grit", verifyCleanAtZeroGrit},
    {"D7 width", verifyWidthGivesAMonoSafeImage},
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
