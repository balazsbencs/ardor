// Objective quality checks for the pitch effects: Whammy, Harmonizer and
// Poly Octave. Each check pins one measured defect from the pitch review.

#include "mod_effect_test_support.h"

#include <complex>
#include <cstdio>
#include <numeric>
#include <utility>

namespace {

using namespace mod_test;

double cents(double ratio) { return 1200.0 * std::log2(ratio); }

// Rendered level of a sub-range of the left channel, relative to a reference
// RMS, in dB.
double rangeRmsDb(const std::vector<float>& x, size_t begin, size_t end)
{
  double sum = 0.0;
  for (size_t i = begin; i < end; ++i) sum += static_cast<double>(x[i]) * x[i];
  return 10.0 * std::log10(std::max(sum / (end - begin), 1e-24));
}

// Strongest component outside +/-15 Hz of `expected`, between 20 Hz and
// 3 kHz, relative to the component at `expected` (dBc).
double spurDbc(const std::vector<float>& x, size_t start, double expected)
{
  constexpr size_t kN = 32768;
  const auto magnitude = [&](double hz) {
    std::complex<double> acc = 0.0;
    for (size_t i = 0; i < kN; ++i) {
      const double window = 0.5 - 0.5 * std::cos(kTwoPi * i / (kN - 1));
      acc += window * static_cast<double>(x[start + i]) * std::polar(1.0, -kTwoPi * hz * i / kSampleRate);
    }
    return std::abs(acc);
  };
  const double wanted = magnitude(expected);
  double worst = 0.0;
  for (double hz = 20.0; hz < 3000.0; hz += 2.0) {
    if (std::fabs(hz - expected) >= 15.0) worst = std::max(worst, magnitude(hz));
  }
  return db(worst / wanted);
}

double meanPitch(const std::vector<float>& x, size_t skip)
{
  const auto track = frequencyTrack(x, skip);
  return std::accumulate(track.begin(), track.end(), 0.0) / track.size();
}

nlohmann::json whammy(int preset, float pedal)
{
  auto params = defaults("whammy");
  params["p2"] = (static_cast<float>(preset) + 0.5f) / 19.0f;
  params["p1"] = pedal;
  return params;
}

// A 196 Hz pluck: attack at 0.1 s, then an exponential decay.
std::vector<float> pluck(size_t frames)
{
  std::vector<float> out(frames, 0.0f);
  for (size_t i = 4800; i < frames; ++i) {
    const double t = (i - 4800) / static_cast<double>(kSampleRate);
    out[i] = static_cast<float>(0.4 * std::exp(-t * 2.0) * std::sin(kTwoPi * 196.0 * t));
  }
  return out;
}

// --- Poly Octave ----------------------------------------------------------------

nlohmann::json polyVoice(const char* key)
{
  auto params = defaults("poly_octave");
  params["p1"] = 0.0f;
  params["p2"] = 0.0f;
  params["depth"] = 0.0f;
  params["p3"] = 0.0f; // Dry
  params[key] = 1.0f;
  return params;
}

// The defaults used to play only the two-octave-down voice, with no dry note,
// 7.3 dB below bypass. A poly octave's default must keep the played note.
void verifyPolyOctaveDefaultKeepsTheNote()
{
  const double level = levelDb(defaults("poly_octave"));
  require(std::fabs(level) < 2.0, "poly octave defaults must stay near bypass level, got " + fmt(level));
  const double dry = levelDb(polyVoice("p3"));
  require(std::fabs(dry) < 0.1, "poly octave Dry at full must pass the note at unity, got " + fmt(dry));
}

// Each voice alone at full level measured -14.0 (up), -6.0 (down) and -4.2 dB
// (down 2) against the dry note. At full they must sit together, near the note.
void verifyPolyOctaveVoicesAreBalanced()
{
  double lo = 1e9, hi = -1e9;
  for (const char* key : {"p1", "p2", "depth"}) {
    const double level = levelDb(polyVoice(key));
    lo = std::min(lo, level);
    hi = std::max(hi, level);
    require(level > -4.0 && level < 1.0, std::string("poly octave voice ") + key +
                                             " at full must sit near the dry level, got " + fmt(level));
  }
  require(hi - lo < 3.0, "poly octave voices must be balanced within 3 dB, spread " + fmt(hi - lo));
}

// Speed ("Tracking") only smoothed knob moves: speed 0 and 1 rendered guitar
// 80 dB apart. It is now Attack, which swells the voices in after each note.
void verifyPolyOctaveAttackSwellsTheVoices()
{
  const auto input = pluck(48000);
  auto immediate = polyVoice("p2");
  immediate["speed"] = 0.0f;
  auto slow = immediate;
  slow["speed"] = 1.0f;
  const auto a = render(immediate, input), b = render(slow, input);
  const size_t onset = 4800, early = onset + 2400;           // first 50 ms of the note
  const size_t lateBegin = onset + 24000, lateEnd = 48000;    // 0.5 s after the note
  const double earlyDrop = rangeRmsDb(b.left, onset, early) - rangeRmsDb(a.left, onset, early);
  const double lateDrop = rangeRmsDb(b.left, lateBegin, lateEnd) - rangeRmsDb(a.left, lateBegin, lateEnd);
  require(earlyDrop < -6.0, "poly octave Attack must soften the note start, early change " + fmt(earlyDrop));
  require(std::fabs(lateDrop) < 2.0, "poly octave Attack must leave the sustain, late change " + fmt(lateDrop));
}

// --- Harmonizer -----------------------------------------------------------------

// A note sung or bent across a semitone boundary must not flip the harmony.
// C major, 3rd up: E harmonises to G (+3), F to A (+4). Vibrato of +/-20 cents
// around E + 50 cents used to switch between the two 29 times in 3 s.
void verifyHarmonizerHoldsItsNoteUnderVibrato()
{
  auto params = defaults("harmonizer");
  params["p1"] = 5.5f / 10.0f;
  params["p2"] = 0.0f;
  params["depth"] = 0.0f;
  params["mix"] = 1.0f;
  const size_t n = 4 * 48000;
  std::vector<float> input(n);
  std::vector<double> instantaneous(n);
  double phase = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double c = 50.0 + 20.0 * std::sin(kTwoPi * 5.0 * i / kSampleRate);
    instantaneous[i] = 329.63 * std::pow(2.0, c / 1200.0);
    phase += kTwoPi * instantaneous[i] / kSampleRate;
    input[i] = 0.3f * static_cast<float>(std::sin(phase));
  }
  const auto out = render(params, input);
  std::vector<float> voice(n);
  for (size_t i = 0; i < n; ++i) voice[i] = out.left[i] - input[i];
  std::vector<double> crossings;
  for (size_t i = 48001; i < n; ++i) {
    if (voice[i - 1] < 0.0f && voice[i] >= 0.0f) {
      crossings.push_back(static_cast<double>(i - 1) + voice[i - 1] / (voice[i - 1] - voice[i]));
    }
  }
  int changes = 0, last = 999;
  for (size_t k = 8; k < crossings.size(); k += 8) {
    const double hz = 8.0 * kSampleRate / (crossings[k] - crossings[k - 8]);
    const size_t at = static_cast<size_t>((crossings[k] + crossings[k - 8]) * 0.5);
    const int shift = static_cast<int>(std::lround(12.0 * std::log2(hz / instantaneous[at])));
    if (last != 999 && shift != last) ++changes;
    last = shift;
  }
  require(changes == 0, "harmonizer must hold one interval under vibrato, it changed " +
                            std::to_string(changes) + " times");
}

// --- Tone loudness --------------------------------------------------------------

// 2 dB rather than the 1.5 dB of the modulation effects: an octave-up voice
// moves the guitar's energy above the tilt's 400 Hz pivot, so the bright end
// measures +1.5 dB on the Whammy. The loop-safe tilt cost 7.5 dB there.
void verifyPitchToneKeepsLoudness()
{
  for (const char* mode : {"whammy", "harmonizer", "poly_octave"}) {
    auto params = std::string(mode) == "whammy" ? whammy(1, 1.0f) : defaults(mode);
    params["tone"] = 0.5f;
    const double flat = levelDb(params);
    for (const float tone : {0.0f, 1.0f}) {
      params["tone"] = tone;
      const double change = levelDb(params) - flat;
      require(std::fabs(change) < 2.0, std::string(mode) + " Tone " + fmt(tone) +
                                           " must keep loudness within 2 dB, got " + fmt(change));
    }
  }
}

// --- Whammy ---------------------------------------------------------------------

// Every other grain restart used to align with its partner one step early,
// always in the same direction: the output ran flat by 1.4 to 4.5 cents and
// carried +/-8 Hz sidebands at -30 dBc around an octave-up note.
void verifyWhammyPitchIsCleanAndInTune()
{
  constexpr double kSemitones[] = {24, 12, 7, 5, -2, -5, -7, -12, -24};
  for (int preset = 0; preset < 9; ++preset) {
    const auto out = render(whammy(preset, 1.0f), sine(196.0, 0.3f, 3 * 48000));
    const double expected = 196.0 * std::pow(2.0, kSemitones[preset] / 12.0);
    const double error = cents(meanPitch(out.left, 48000) / expected);
    require(std::fabs(error) < 1.5, "whammy preset " + std::to_string(preset) + " must be in tune, " +
                                        fmt(error) + " cents");
    const double spur = spurDbc(out.left, 48000, expected);
    require(spur < -50.0, "whammy preset " + std::to_string(preset) +
                              " must not add grain sidebands, worst " + fmt(spur) + " dBc");
  }
}

void verifyWhammyTakesStereoInput()
{
  const auto& phrase = guitarPhrase();
  std::vector<float> inverted(phrase.size());
  std::transform(phrase.begin(), phrase.end(), inverted.begin(), [](float x) { return -x; });
  for (const int preset : {1, 12}) { // 1 Oct up (Whammy), 4th dn / 3rd dn (Harmony)
    const auto params = whammy(preset, 0.5f);
    const double mono = rmsDb(renderStereo(params, phrase, phrase));
    const double antiPhase = rmsDb(renderStereo(params, phrase, inverted));
    require(std::fabs(antiPhase - mono) < 1.5, "whammy preset " + std::to_string(preset) +
                " must process stereo input, anti-phase level differs by " + fmt(antiPhase - mono));
  }
}

// Envelope beat rate of a steady tone: the detuned voice beats against the dry
// note at the frequency difference between them.
double beatRateHz(const Render& out)
{
  constexpr size_t kBlock = 480;
  std::vector<double> env;
  for (size_t start = 48000; start + kBlock <= out.left.size(); start += kBlock) {
    double sum = 0.0;
    for (size_t i = start; i < start + kBlock; ++i) sum += static_cast<double>(out.left[i]) * out.left[i];
    env.push_back(std::sqrt(sum / kBlock));
  }
  const double mean = std::accumulate(env.begin(), env.end(), 0.0) / env.size();
  int crossings = 0;
  for (size_t i = 1; i < env.size(); ++i) {
    if (env[i - 1] < mean && env[i] >= mean) ++crossings;
  }
  return crossings / (env.size() * kBlock / static_cast<double>(kSampleRate));
}

// The Whammy's Detune modes double the note with a slightly detuned copy.
// 196 Hz beats at about 1 Hz against an 8-cent copy and 2.3 Hz against 20.
void verifyWhammyDetune()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "whammy");
  require(descriptor != nullptr && descriptor->params.size() >= 8 && descriptor->params[7].key == "p3",
          "whammy must have a Detune control");
  auto shallow = whammy(1, 0.5f);
  shallow["p3"] = 0.5f;
  auto deep = shallow;
  deep["p3"] = 1.0f;
  const auto input = sine(196.0, 0.3f, 10 * 48000);
  const double shallowRate = beatRateHz(render(shallow, input));
  const double deepRate = beatRateHz(render(deep, input));
  require(shallowRate > 0.4 && shallowRate < 2.0, "whammy Shallow must beat near 1 Hz, got " + fmt(shallowRate));
  require(deepRate > shallowRate * 1.8, "whammy Deep must detune further than Shallow, " +
                                            fmt(shallowRate) + " and " + fmt(deepRate) + " Hz");
  require(channelDifference(render(deep, input)) > 1e-3, "whammy Detune must spread across the channels");
}

// Switching from a shifted preset to Detune glides both voices from the old
// interval. The right voice reads at the left voice's ratio times the detune
// spread, but its anti-alias filter was set for the mirrored (downward) pitch,
// where it is off: during the glide it passed material that its upward read
// then folded back across Nyquist (review of #88).
double detuneSwitchVoiceBalanceDb()
{
  auto params = whammy(1, 1.0f); // 1 Oct up at the toe
  params["speed"] = 0.0f;        // slowest glide
  params["mix"] = 1.0f;
  params["depth"] = 1.0f;
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("mod", params, kSampleRate, error), error);
  const auto input = sine(15000.0, 0.3f, 2 * 48000);
  double left = 0.0, right = 0.0;
  for (size_t i = 0; i < input.size(); ++i) {
    if (i == 48000) processor.setParameterTarget("p3", 1.0f); // Deep
    const auto y = processor.process({input[i], input[i]});
    // Detune carries the dry note; subtract it to leave each voice. Both
    // voices read upward at nearly the same ratio during the glide, so with
    // their filters set alike they carry nearly the same energy.
    if (i >= 48000 + 480 && i < 48000 + 2880) {
      left += static_cast<double>(y.left - input[i]) * (y.left - input[i]);
      right += static_cast<double>(y.right - input[i]) * (y.right - input[i]);
    }
  }
  return 10.0 * std::log10(std::max(right, 1e-30) / std::max(left, 1e-30));
}

void verifyWhammyDetuneSwitchFiltersBothVoices()
{
  const double balance = detuneSwitchVoiceBalanceDb();
  require(std::fabs(balance) < 3.0, "whammy Detune switch must filter both voices alike, right voice " +
                                        fmt(balance) + " dB against the left");
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"poly octave default", verifyPolyOctaveDefaultKeepsTheNote},
    {"poly octave balance", verifyPolyOctaveVoicesAreBalanced},
    {"poly octave attack", verifyPolyOctaveAttackSwellsTheVoices},
    {"harmonizer hysteresis", verifyHarmonizerHoldsItsNoteUnderVibrato},
    {"pitch tone loudness", verifyPitchToneKeepsLoudness},
    {"whammy pitch", verifyWhammyPitchIsCleanAndInTune},
    {"whammy stereo", verifyWhammyTakesStereoInput},
    {"whammy detune", verifyWhammyDetune},
    {"whammy detune switch", verifyWhammyDetuneSwitchFiltersBothVoices},
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
