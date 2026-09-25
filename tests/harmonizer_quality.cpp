// Harmonizer response and voicing. Each check pins a measured shortcoming
// from the pitch review: slow note tracking, a single voice, a mono dry path.

#include "mod_effect_test_support.h"

#include <complex>
#include <cstdio>
#include <utility>

namespace {

using namespace mod_test;

double cents(double ratio) { return 1200.0 * std::log2(ratio); }

// Index of an interval on the 10-step Interval selector (see kIntervalDegrees).
float intervalValue(int index) { return (static_cast<float>(index) + 0.5f) / 10.0f; }
constexpr int kThirdUp = 5;
constexpr int kFifthUp = 7;

nlohmann::json cMajorThirdUp()
{
  auto params = defaults("harmonizer");
  params["p1"] = intervalValue(kThirdUp);
  params["p2"] = 0.0f;    // key of C
  params["depth"] = 0.0f; // major
  params["mix"] = 1.0f;
  return params;
}

std::vector<float> tone(double hz, size_t begin, size_t end, size_t frames)
{
  std::vector<float> out(frames, 0.0f);
  for (size_t i = begin; i < end && i < frames; ++i) {
    out[i] = 0.3f * static_cast<float>(std::sin(kTwoPi * hz * (i - begin) / kSampleRate));
  }
  return out;
}

// Milliseconds after `from` until the harmony voice holds within 20 cents of
// `target` for five consecutive periods.
double settleMs(const nlohmann::json& params, const std::vector<float>& input, size_t from,
                double target)
{
  const auto out = render(params, input);
  std::vector<float> voice(input.size());
  for (size_t i = 0; i < input.size(); ++i) voice[i] = out.left[i] - input[i];
  std::vector<double> crossings;
  for (size_t i = from + 1; i < voice.size(); ++i) {
    if (voice[i - 1] < 0.0f && voice[i] >= 0.0f) {
      crossings.push_back(static_cast<double>(i - 1) + voice[i - 1] / (voice[i - 1] - voice[i]));
    }
  }
  for (size_t k = 1; k + 5 < crossings.size(); ++k) {
    bool steady = true;
    for (size_t j = k; j < k + 5; ++j) {
      const double hz = kSampleRate / (crossings[j] - crossings[j - 1]);
      steady = steady && std::fabs(cents(hz / target)) < 20.0;
    }
    if (steady) return (crossings[k - 1] - static_cast<double>(from)) * 1000.0 / kSampleRate;
  }
  return 1e9;
}

// A new note used to take 107 ms to get its harmony, and the first note after
// silence 80 ms: long enough to hear the old interval on the new note.
void verifyHarmonizerFollowsNotesQuickly()
{
  constexpr size_t kChange = 48000;
  auto input = tone(220.0, 0, kChange, 2 * kChange);             // A3
  const auto second = tone(261.63, kChange, 2 * kChange, 2 * kChange); // C4
  for (size_t i = kChange; i < input.size(); ++i) input[i] = second[i];
  // In C major a third above A is C (+3), above C it is E (+4).
  const double change = settleMs(cMajorThirdUp(), input, kChange, 329.63);
  require(change < 45.0, "harmony must follow a note change within 45 ms, took " + fmt(change));

  const auto first = tone(220.0, 24000, 96000, 96000);
  const double onset = settleMs(cMajorThirdUp(), first, 24000, 261.63);
  require(onset < 45.0, "harmony must find the first note within 45 ms, took " + fmt(onset));
}

double componentDb(const std::vector<float>& x, size_t start, double hz)
{
  constexpr size_t kN = 32768;
  std::complex<double> acc = 0.0;
  for (size_t i = 0; i < kN; ++i) {
    const double window = 0.5 - 0.5 * std::cos(kTwoPi * i / (kN - 1));
    acc += window * static_cast<double>(x[start + i]) * std::polar(1.0, -kTwoPi * hz * i / kSampleRate);
  }
  return db(std::abs(acc));
}

// Studio harmonizers stack two voices. Interval 2 (p3) adds the second, and
// Voice 2 Level (p4) sets it against the first.
void verifyHarmonizerSecondVoice()
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", "harmonizer");
  require(descriptor != nullptr && descriptor->params.size() == 9,
          "harmonizer must have Interval 2 and Voice 2 Level");
  const auto input = tone(261.63, 0, 3 * 48000, 3 * 48000); // C4

  auto single = cMajorThirdUp();
  auto legacy = single;
  legacy.erase("p3");
  legacy.erase("p4");
  require(maxDifference(single, legacy) == 0.0, "harmonizer Interval 2 must default to Off");

  auto both = cMajorThirdUp();
  both["p3"] = (static_cast<float>(kFifthUp) + 1.5f) / 11.0f; // Off + 10 intervals
  both["p4"] = 1.0f;
  const auto out = render(both, input);
  const double third = componentDb(out.left, 48000, 329.63) + componentDb(out.right, 48000, 329.63);
  const double fifth = componentDb(out.left, 48000, 392.00) + componentDb(out.right, 48000, 392.00);
  require(std::fabs(third - fifth) < 6.0, "both harmony voices must sound, third " + fmt(third) +
                                              " vs fifth " + fmt(fifth));
  require(channelDifference(out) > 1e-3, "two voices must spread across the channels");

  // A silent second voice must not move the first: selecting Interval 2 with
  // its level at zero used to pan Voice 1 to the left (review of #89).
  auto silent = cMajorThirdUp();
  silent["p3"] = both["p3"];
  silent["p4"] = 0.0f;
  require(maxDifference(single, silent) == 0.0,
          "harmonizer with a silent Voice 2 must sound exactly like one voice");

  both["p4"] = 0.0f;
  const auto quiet = render(both, input);
  const double quietFifth = componentDb(quiet.left, 48000, 392.00);
  require(quietFifth < componentDb(quiet.left, 48000, 329.63) - 40.0,
          "Voice 2 Level at zero must silence the second voice");
}

void verifyHarmonizerKeepsStereoDry()
{
  const auto& phrase = guitarPhrase();
  std::vector<float> inverted(phrase.size());
  std::transform(phrase.begin(), phrase.end(), inverted.begin(), [](float x) { return -x; });
  // Mix at full so the mode's own output is heard; at zero the host passes
  // the stereo dry straight through and would hide a mono dry path.
  const auto params = cMajorThirdUp();
  const double mono = rmsDb(renderStereo(params, phrase, phrase));
  const double antiPhase = rmsDb(renderStereo(params, phrase, inverted));
  require(std::fabs(antiPhase - mono) < 1.5, "harmonizer must process stereo input, anti-phase differs by " +
                                                 fmt(antiPhase - mono) + " dB");
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"tracking speed", verifyHarmonizerFollowsNotesQuickly},
    {"second voice", verifyHarmonizerSecondVoice},
    {"stereo dry", verifyHarmonizerKeepsStereoDry},
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
