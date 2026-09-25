// Physical-model behaviour of the classic modulation effects.
//
// Each check names a property of the hardware the effect imitates, and fails
// on a model that lacks it: a lamp's thermal lag, a photocell's fast attack
// and slow release, a Leslie's brake, an ensemble's unrelated voices.

#include "mod_effect_test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace mod_test;

// Normalized Speed for a physical rate on the default 0.05..10 Hz, t^2 range.
float speedFor(double hz) { return static_cast<float>(std::sqrt((hz - 0.05) / 9.95)); }

// 1 ms block-RMS envelope of the left channel, after one second of settling.
std::vector<double> envelope(const Render& out)
{
  constexpr size_t kBlock = 48;
  std::vector<double> env;
  for (size_t start = 48000; start + kBlock <= out.left.size(); start += kBlock) {
    double sum = 0.0;
    for (size_t i = start; i < start + kBlock; ++i) sum += out.left[i] * out.left[i];
    env.push_back(std::sqrt(sum / kBlock));
  }
  return env;
}

double swingDb(const std::vector<double>& env)
{
  const auto [lo, hi] = std::minmax_element(env.begin(), env.end());
  return db(*hi / std::max(*lo, 1e-9));
}

// Share of the time the envelope is falling. A time-symmetric modulator
// spends half its cycle falling; a fast attack and slow release do not.
double fallingShare(const std::vector<double>& env)
{
  size_t falling = 0, moving = 0;
  for (size_t i = 1; i < env.size(); ++i) {
    const double step = env[i] - env[i - 1];
    if (std::fabs(step) < 1e-7) continue;
    ++moving;
    if (step < 0.0) ++falling;
  }
  return moving == 0 ? 0.5 : static_cast<double>(falling) / moving;
}

// --- Vibe ---------------------------------------------------------------------

// A Uni-Vibe's sweep comes from one incandescent lamp. Its filament cannot
// follow a fast oscillator, so the sweep narrows as Speed rises; the old model
// swept the same width at every speed.
void verifyVibeLampLag()
{
  const auto swingAt = [](double hz) {
    auto params = defaults("vibe");
    params["mix"] = 0.5f;
    params["depth"] = 1.0f;
    params["speed"] = speedFor(hz);
    return swingDb(envelope(render(params, sine(600.0, 0.3f, 6 * 48000))));
  };
  const double slow = swingAt(1.0);
  const double fast = swingAt(8.0);
  require(fast < slow - 2.0, "vibe sweep must narrow at speed (lamp lag), swing " + fmt(slow) +
                                 " dB at 1 Hz and " + fmt(fast) + " dB at 8 Hz");
}

// --- Vintage Trem -----------------------------------------------------------------

// An optical tremolo's photocell darkens slowly: the level drops fast when the
// lamp fires and recovers slowly, so it spends well under half its time falling.
void verifyPhotoTremAttackRelease()
{
  auto params = defaults("vintage_trem");
  params["p2"] = 1.0f; // Photoresistor
  params["depth"] = 1.0f;
  params["speed"] = 0.1f;
  const auto out = render(params, std::vector<float>(6 * 48000, 0.5f));
  const double share = fallingShare(envelope(out));
  require(share < 0.4, "photoresistor trem must drop fast and recover slowly, falling share " +
                           fmt(share));
}

// --- Rotary -------------------------------------------------------------------------

void verifyRotaryBrake()
{
  auto params = defaults("rotary");
  params["p2"] = 0.5f; // Stop
  params["depth"] = 1.0f;
  const auto out = render(params, sine(3000.0, 0.3f, 10 * 48000));
  const auto track = frequencyTrack(out.left, 8 * 48000);
  const auto [lo, hi] = std::minmax_element(track.begin(), track.end());
  const double swing = (*hi - *lo) / 3000.0;
  require(swing < 0.002, "rotary Stop must bring the rotors to rest, pitch swing " + fmt(swing));

  const auto* rotary = ardor::findDaisyFxDescriptor("mod", "rotary");
  const auto& rotor = rotary->params[5];
  require(ardor::formatDaisyFxParamValue(*rotary, rotor, 0.0f) == "Slow"
              && ardor::formatDaisyFxParamValue(*rotary, rotor, 0.5f) == "Stop"
              && ardor::formatDaisyFxParamValue(*rotary, rotor, 1.0f) == "Fast",
          "rotary Rotor must read Slow / Stop / Fast, keeping saved Fast at 1.0");
}

double toneGainDb(nlohmann::json params, double hz)
{
  const auto out = render(params, sine(hz, 0.3f, 2 * 48000));
  double sum = 0.0;
  for (size_t i = 48000; i < out.left.size(); ++i) sum += out.left[i] * out.left[i];
  return db(std::sqrt(sum / (out.left.size() - 48000)) / (0.3 / std::sqrt(2.0)));
}

// Tone is the cabinet's brightness; the crossover stays at the Leslie's 800 Hz.
void verifyRotaryToneIsBrightness()
{
  auto params = defaults("rotary");
  params["depth"] = 0.0f;
  params["tone"] = 0.0f;
  const double darkTilt = toneGainDb(params, 5000.0) - toneGainDb(params, 150.0);
  params["tone"] = 1.0f;
  const double brightTilt = toneGainDb(params, 5000.0) - toneGainDb(params, 150.0);
  require(brightTilt > darkTilt + 8.0, "rotary Tone must tilt the cabinet, tilt " + fmt(darkTilt) +
                                           " dB dark and " + fmt(brightTilt) + " dB bright");
}

// --- Chorus -------------------------------------------------------------------------

// An ensemble gives each side its own voices. Multi used to send one voice to
// both channels, which measured 0.49 L/R correlation on guitar, the narrowest
// of the chorus types (dBucket 0.17, Digital -0.02).
void verifyChorusMultiIsAnEnsemble()
{
  auto params = defaults("chorus");
  params["p2"] = 0.25f; // Multi
  params["mix"] = 1.0f;
  const auto out = render(params, guitarPhrase());
  double lr = 0.0, ll = 0.0, rr = 0.0;
  for (size_t i = 0; i < out.left.size(); ++i) {
    lr += out.left[i] * out.right[i];
    ll += out.left[i] * out.left[i];
    rr += out.right[i] * out.right[i];
  }
  const double correlation = lr / std::sqrt(ll * rr);
  require(correlation < 0.25, "chorus Multi must give each side its own voices, L/R correlation " +
                                  fmt(correlation));
}

// --- Pattern Trem ---------------------------------------------------------------------

void verifyPatternsAreNamedAndLive()
{
  const auto* pattern = ardor::findDaisyFxDescriptor("mod", "pattern_trem");
  const auto& control = pattern->params[4];
  const auto spec = ardor::daisyFxParamControlSpec(*pattern, control);
  std::vector<std::string> names;
  for (const float value : spec.choiceValues) {
    names.push_back(ardor::formatDaisyFxParamValue(*pattern, control, value));
    require(names.back().rfind("Pattern ", 0) != 0, "pattern trem patterns must have names, got " +
                                                        names.back());
  }
  auto sorted = names;
  std::sort(sorted.begin(), sorted.end());
  require(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(),
          "pattern trem pattern names must be unique");

  // Every pattern must actually gate the signal; one used to be all on.
  for (size_t index = 0; index < spec.choiceValues.size(); ++index) {
    auto params = defaults("pattern_trem");
    params["depth"] = 1.0f;
    params["p1"] = spec.choiceValues[index];
    const auto out = render(params, std::vector<float>(3 * 48000, 0.5f));
    const float quietest = *std::min_element(out.left.begin() + 48000, out.left.end());
    require(quietest < 0.05f, "pattern " + names[index] + " must gate the signal");
  }
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"vibe lamp lag", verifyVibeLampLag},
    {"photo trem attack/release", verifyPhotoTremAttackRelease},
    {"rotary brake", verifyRotaryBrake},
    {"rotary tone", verifyRotaryToneIsBrightness},
    {"chorus ensemble", verifyChorusMultiIsAnEnsemble},
    {"pattern names", verifyPatternsAreNamedAndLive},
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
