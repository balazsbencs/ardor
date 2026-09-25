// Objective quality checks for the classic modulation effects: Chorus,
// Flanger, Phaser, Vibe, Rotary, Vintage Trem and Pattern Trem.
//
// Each check pins one measured defect from the modulation review, so that it
// cannot come back unnoticed. The thresholds are physical (dB, dBc, Hz,
// percent of pitch), not snapshots of a particular implementation.

#include "mod_effect_test_support.h"

#include "daisyfx/hosted/dsp/freq_table.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace mod_test;

// --- Flanger --------------------------------------------------------------

void verifyThroughZeroFlangerHasNoUndelayedDry()
{
  for (const float mix : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f}) {
    for (const float type : {0.7f, 1.0f}) {
      auto params = defaults("flanger");
      params["p2"] = type;
      params["mix"] = mix;
      params["depth"] = 0.0f;
      params["p1"] = 0.0f;
      auto processor = configured(params);
      for (int i = 0; i < 9600; ++i) (void)processor.process({0.0f, 0.0f});
      const auto impulse = processor.process({1.0f, 1.0f});
      // A through-zero flanger delays its dry path to meet the wet tap. Any
      // undelayed dry left over combs against it and ruins the zero point.
      require(std::fabs(impulse.left) < 0.01f && std::fabs(impulse.right) < 0.01f,
              "through-zero flanger leaks undelayed dry at mix " + fmt(mix) + ": " +
                  fmt(impulse.left));
    }
  }
}

// --- Rotary ---------------------------------------------------------------

void verifyRotaryKeepsLevel()
{
  for (const float depth : {0.0f, 0.7f, 1.0f}) {
    for (const float rotor : {0.0f, 1.0f}) {
      auto params = defaults("rotary");
      params["depth"] = depth;
      params["p2"] = rotor;
      const double level = levelDb(params);
      require(std::fabs(level) < 1.5,
              "rotary engaged level must stay within 1.5 dB of bypass, got " + fmt(level) +
                  " dB at depth " + fmt(depth));
    }
  }
}

// The two rotor bands move against each other, so the crossover must not let
// them sum above the input when their relative delay changes. A steady tone
// through the crossover region shows this directly: its level may swing with
// the horn (make-up and honk), but must never stand far above the input.
// Transient peaks of a guitar are not the measure here: any crossover's phase
// reshapes a pick attack, and a real Leslie's does too.
void verifyRotaryBandsDoNotSumAboveInput()
{
  for (const double hz : {300.0, 500.0, 800.0, 1250.0, 2000.0, 3000.0}) {
    for (const float tone : {0.0f, 0.5f, 1.0f}) {
      auto params = defaults("rotary");
      params["depth"] = 1.0f;
      params["p2"] = 1.0f;
      params["tone"] = tone;
      constexpr float kAmplitude = 0.3f;
      const auto out = render(params, sine(hz, kAmplitude, 6 * 48000));
      float peak = 0.0f;
      for (size_t i = 4 * 48000; i < out.left.size(); ++i) {
        peak = std::max({peak, std::fabs(out.left[i]), std::fabs(out.right[i])});
      }
      const double gain = db(peak / kAmplitude);
      require(gain < 4.0, "rotary must not sum above the input: " + fmt(gain) + " dB at " +
                              fmt(hz) + " Hz, tone " + fmt(tone));
    }
  }
}

void verifyRotaryLabelsAreDistinct()
{
  const auto* rotary = ardor::findDaisyFxDescriptor("mod", "rotary");
  require(rotary != nullptr, "rotary descriptor exists");
  for (size_t i = 0; i < rotary->params.size(); ++i) {
    for (size_t j = i + 1; j < rotary->params.size(); ++j) {
      require(rotary->params[i].label != rotary->params[j].label,
              "rotary has two controls named " + rotary->params[i].label);
    }
  }
}

struct PitchModulation {
  double ratePeakToPeak = 0.0; // fractional pitch change, peak to peak
  double rateHz = 0.0;         // how often the pitch goes round
};

PitchModulation rotaryPitch(double probeHz, float depth)
{
  auto params = defaults("rotary");
  params["p2"] = 1.0f; // fast
  params["depth"] = depth;
  const auto out = render(params, sine(probeHz, 0.3f, 12 * 48000));
  // Skip the motor spin-up, then analyse four seconds.
  const auto track = frequencyTrack(out.left, 8 * 48000);
  std::vector<double> smooth;
  const size_t window = std::max<size_t>(1, static_cast<size_t>(probeHz / 400.0));
  for (size_t i = window; i < track.size(); ++i) {
    double sum = 0.0;
    for (size_t k = i - window; k < i; ++k) sum += track[k];
    smooth.push_back(sum / window);
  }
  const auto [lo, hi] = std::minmax_element(smooth.begin(), smooth.end());
  // Count how often the track crosses its mean upwards to find the rotor rate.
  double mean = 0.0;
  for (const double v : smooth) mean += v;
  mean /= smooth.size();
  int cycles = 0;
  for (size_t i = 1; i < smooth.size(); ++i) {
    if (smooth[i - 1] < mean && smooth[i] >= mean) ++cycles;
  }
  const double seconds = smooth.size() / probeHz;
  return {(*hi - *lo) / probeHz, cycles / seconds};
}

void verifyRotaryFastSpeedAndDoppler()
{
  // A Leslie 122 horn runs near 400 rpm (6.7 Hz) on fast. With a rotor radius
  // near 0.17 m its Doppler pitch swing is about +/-2 %, so 4 % peak to peak.
  const auto horn = rotaryPitch(3000.0, 1.0f);
  require(horn.rateHz > 6.0 && horn.rateHz < 7.4,
          "rotary fast horn must turn near 6.7 Hz, got " + fmt(horn.rateHz));
  require(horn.ratePeakToPeak > 0.02 && horn.ratePeakToPeak < 0.06,
          "rotary horn Doppler at full depth must be about 4 % peak to peak, got " +
              fmt(horn.ratePeakToPeak));
  // The drum turns near 340 rpm (5.7 Hz) on fast, and its baffle moves the
  // pitch less than the horn does.
  const auto drum = rotaryPitch(150.0, 1.0f);
  require(drum.rateHz > 5.0 && drum.rateHz < 6.4,
          "rotary fast drum must turn near 5.7 Hz, got " + fmt(drum.rateHz));
  require(drum.ratePeakToPeak < horn.ratePeakToPeak,
          "rotary drum Doppler must be smaller than the horn's, got " +
              fmt(drum.ratePeakToPeak));
}

// --- Vintage Trem -----------------------------------------------------------

void verifyVintageTremIsClean()
{
  for (const float type : {0.0f, 1.0f}) {
    auto params = defaults("vintage_trem");
    params["p2"] = type;
    params["depth"] = 0.0f;
    params["tone"] = 0.5f;
    const auto out = render(params, sine(1000.0, 0.5f, 2 * 48000));
    const double h3 = harmonicDbc(out.left, 1000.0, 3);
    require(h3 < -80.0, "vintage trem must not distort a clean signal, H3 " + fmt(h3) + " dBc");
  }
  auto params = defaults("vintage_trem");
  params["depth"] = 1.0f;
  params["tone"] = 0.5f;
  const auto out = render(params, sine(1000.0, 0.5f, 2 * 48000));
  float peak = 0.0f;
  for (const float v : out.left) peak = std::max(peak, std::fabs(v));
  require(db(peak / 0.5f) < 3.1, "vintage trem make-up must keep peaks within +3 dB, got " +
                                     fmt(db(peak / 0.5f)));
}

// --- Tone loudness ------------------------------------------------------------

void verifyToneKeepsLoudness()
{
  for (const char* mode : {"chorus", "vibe", "vintage_trem", "pattern_trem"}) {
    auto params = defaults(mode);
    if (std::string(mode) != "chorus") params["depth"] = 0.0f;
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

// --- Chorus -------------------------------------------------------------------

void verifyChorusControlsAreLive()
{
  for (const float type : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
    auto dark = defaults("chorus");
    dark["p2"] = type;
    dark["tone"] = 0.0f;
    auto bright = dark;
    bright["tone"] = 1.0f;
    require(maxDifference(dark, bright) > 1e-3, "chorus Tone must work in type " + fmt(type));
  }
  auto vibrato = defaults("chorus");
  vibrato["p2"] = 0.5f;
  auto digital = vibrato;
  digital["p2"] = 1.0f;
  require(maxDifference(vibrato, digital) > 1e-3, "chorus Vibrato must differ from Digital");

  auto shortDetune = defaults("chorus");
  shortDetune["p2"] = 0.75f;
  shortDetune["p1"] = 0.0f;
  auto longDetune = shortDetune;
  longDetune["p1"] = 1.0f;
  require(maxDifference(shortDetune, longDetune) > 1e-3, "chorus Delay must work in Detune");
}

// --- Phaser -------------------------------------------------------------------

void verifyAllpassReachesTheTopOfTheSweep()
{
  using pedal::freq_table::allpass_coeff_at;
  using pedal::freq_table::position_for_hz;
  const float at12k = allpass_coeff_at(position_for_hz(12000.0f));
  const float at16k = allpass_coeff_at(position_for_hz(16000.0f));
  require(at16k > at12k + 0.05f,
          "allpass corner must keep rising above 12 kHz, got " + fmt(at12k) + " and " + fmt(at16k));
}

// --- Stereo input and mix level -----------------------------------------------

// These effects once summed L and R before processing, so a stereo source
// partly cancelled and an anti-phase one vanished entirely. An anti-phase
// input must come out as loud as a mono one.
void verifyStereoInputIsNotCancelled()
{
  const auto& phrase = guitarPhrase();
  std::vector<float> inverted(phrase.size());
  std::transform(phrase.begin(), phrase.end(), inverted.begin(), [](float x) { return -x; });
  for (const char* mode : {"chorus", "phaser", "vibe", "rotary"}) {
    const auto params = defaults(mode);
    const double mono = rmsDb(renderStereo(params, phrase, phrase));
    const double antiPhase = rmsDb(renderStereo(params, phrase, inverted));
    require(std::fabs(antiPhase - mono) < 1.5, std::string(mode) +
                " must process stereo input, anti-phase level differs by " + fmt(antiPhase - mono) +
                " dB");
  }
}

// A delayed, modulated copy is largely uncorrelated with the dry signal, so a
// linear 50/50 blend loses about 3 dB. Chorus and Flanger own their blend and
// must hold their level across the Mix control, in every type.
//
// 1.5 dB, not less: a short delay is not uncorrelated at guitar fundamentals.
// dBucket's 3 ms puts its first comb notch near 167 Hz, which measures -1.4 dB
// at 50 % Mix. That comb is the sound of the circuit, not a gain error.
void verifyMixKeepsLevel()
{
  for (const char* mode : {"chorus", "flanger"}) {
    for (const float type : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
      for (const float mix : {0.25f, 0.5f, 0.75f, 1.0f}) {
        auto params = defaults(mode);
        params["p2"] = type;
        params["mix"] = mix;
        const double level = levelDb(params);
        require(std::fabs(level) < 1.5, std::string(mode) + " type " + fmt(type) + " at mix " +
                                            fmt(mix) + " must stay within 1.5 dB of bypass, got " +
                                            fmt(level));
      }
    }
  }
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"through-zero flanger", verifyThroughZeroFlangerHasNoUndelayedDry},
    {"rotary level", verifyRotaryKeepsLevel},
    {"rotary band sum", verifyRotaryBandsDoNotSumAboveInput},
    {"rotary labels", verifyRotaryLabelsAreDistinct},
    {"rotary speed and Doppler", verifyRotaryFastSpeedAndDoppler},
    {"vintage trem distortion", verifyVintageTremIsClean},
    {"tone loudness", verifyToneKeepsLoudness},
    {"chorus controls", verifyChorusControlsAreLive},
    {"phaser sweep top", verifyAllpassReachesTheTopOfTheSweep},
    {"stereo input", verifyStereoInputIsNotCancelled},
    {"mix level", verifyMixKeepsLevel},
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
