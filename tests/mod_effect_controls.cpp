// Behaviour of the extra modulation controls (p3 and p4).
//
// Each new control must do what its label says, and its default must leave
// the effect sounding as it did before the control existed, so that saved
// presets, which do not carry p3 or p4, are unchanged.

#include "mod_effect_test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace mod_test;

// Scene targets address Daisy parameters by descriptor index, so the seven
// original controls must keep their slots and the new ones come after them.
void verifyDescriptorIndexesAreStable()
{
  constexpr std::array<const char*, 9> kKeys{"speed", "depth", "mix", "tone", "p1",
                                             "p2", "level", "p3", "p4"};
  for (const auto& descriptor : ardor::daisyFxCatalog()) {
    if (descriptor.kind != ardor::DaisyFxKind::Mod) continue;
    require(descriptor.params.size() >= 7 && descriptor.params.size() <= kKeys.size(),
            descriptor.mode + " has an unexpected control count");
    for (size_t i = 0; i < descriptor.params.size(); ++i) {
      require(descriptor.params[i].key == kKeys[i],
              descriptor.mode + " control " + std::to_string(i) + " must be " + kKeys[i]);
    }
  }
}

bool hasControl(const std::string& mode, const std::string& key)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", mode);
  require(descriptor != nullptr, mode + " descriptor exists");
  return std::any_of(descriptor->params.begin(), descriptor->params.end(),
                     [&](const auto& param) { return param.key == key; });
}

// A preset saved before p3/p4 existed must render exactly as one that spells
// out their defaults.
void verifyMissingControlsFallBackToDefaults()
{
  for (const char* mode : {"flanger", "phaser", "chorus", "vintage_trem", "pattern_trem", "rotary"}) {
    const auto explicitDefaults = defaults(mode);
    require(explicitDefaults.contains("p3"), std::string(mode) + " must have a p3 control");
    auto legacy = explicitDefaults;
    legacy.erase("p3");
    legacy.erase("p4");
    require(maxDifference(explicitDefaults, legacy) == 0.0,
            std::string(mode) + " must treat a missing p3/p4 as its default");
  }
}

// Position of the largest output sample after an impulse, on the left.
size_t impulseLag(const nlohmann::json& params)
{
  auto processor = configured(params);
  for (int i = 0; i < 9600; ++i) (void)processor.process({0.0f, 0.0f});
  size_t lag = 0;
  float peak = 0.0f;
  for (size_t i = 0; i < 1024; ++i) {
    const auto y = processor.process({i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f});
    if (std::fabs(y.left) > peak) {
      peak = std::fabs(y.left);
      lag = i;
    }
  }
  return lag;
}

void verifyFlangerManualMovesTheSweep()
{
  require(hasControl("flanger", "p3"), "flanger has Manual");
  auto params = defaults("flanger");
  params["depth"] = 0.0f;
  params["p1"] = 0.0f;
  params["mix"] = 1.0f;
  params["p3"] = 0.25f;
  const size_t low = impulseLag(params);
  params["p3"] = 0.75f;
  const size_t high = impulseLag(params);
  require(low + 50 < high, "flanger Manual must move the delay, got " + std::to_string(low) +
                               " and " + std::to_string(high) + " samples");
}

void verifyStereoControlCanCollapseToMono(const std::string& mode, const std::string& key,
                                          float mono, float wide)
{
  require(hasControl(mode, key), mode + " has a stereo control");
  const std::vector<float> input(guitarPhrase().begin(), guitarPhrase().begin() + 96000);
  auto params = defaults(mode);
  params[key] = mono;
  require(channelDifference(render(params, input)) < 1e-6,
          mode + " " + key + " at " + fmt(mono) + " must give identical channels");
  params[key] = wide;
  require(channelDifference(render(params, input)) > 1e-3,
          mode + " " + key + " at " + fmt(wide) + " must give different channels");
}

void verifyStereoControls()
{
  verifyStereoControlCanCollapseToMono("flanger", "p4", 0.0f, 1.0f);
  verifyStereoControlCanCollapseToMono("phaser", "p3", 0.0f, 1.0f);
  verifyStereoControlCanCollapseToMono("chorus", "p3", 0.0f, 1.0f);
  verifyStereoControlCanCollapseToMono("vintage_trem", "p3", 0.0f, 1.0f);
}

void verifyPhaserPolarity()
{
  require(hasControl("phaser", "p4"), "phaser has Polarity");
  auto positive = defaults("phaser");
  positive["p1"] = 0.8f;
  positive["p4"] = 0.0f;
  auto negative = positive;
  negative["p4"] = 1.0f;
  require(maxDifference(positive, negative) > 1e-3, "phaser Polarity must change the sound");
}

// Block-RMS envelopes of the two channels for a steady tone.
std::pair<std::vector<double>, std::vector<double>> envelopes(const Render& out)
{
  constexpr size_t kBlock = 480;
  std::vector<double> left, right;
  for (size_t start = 48000; start + kBlock <= out.left.size(); start += kBlock) {
    double l = 0.0, r = 0.0;
    for (size_t i = start; i < start + kBlock; ++i) {
      l += static_cast<double>(out.left[i]) * out.left[i];
      r += static_cast<double>(out.right[i]) * out.right[i];
    }
    left.push_back(std::sqrt(l / kBlock));
    right.push_back(std::sqrt(r / kBlock));
  }
  return {left, right};
}

double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
  double ma = 0.0, mb = 0.0;
  for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
  ma /= a.size();
  mb /= b.size();
  double ab = 0.0, aa = 0.0, bb = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    ab += (a[i] - ma) * (b[i] - mb);
    aa += (a[i] - ma) * (a[i] - ma);
    bb += (b[i] - mb) * (b[i] - mb);
  }
  return ab / std::sqrt(aa * bb);
}

void verifyVintageTremPans()
{
  auto params = defaults("vintage_trem");
  params["depth"] = 1.0f;
  params["p3"] = 1.0f;
  const auto [left, right] = envelopes(render(params, sine(1000.0, 0.3f, 4 * 48000)));
  const double r = correlation(left, right);
  require(r < -0.9, "vintage trem Stereo at full must pan L against R, correlation " + fmt(r));
}

// Largest sample-to-sample change of the tremolo gain on a DC input.
double largestGainStep(nlohmann::json params)
{
  const auto out = render(params, std::vector<float>(2 * 48000, 0.5f));
  double step = 0.0;
  for (size_t i = 1; i < out.left.size(); ++i) {
    step = std::max(step, static_cast<double>(std::fabs(out.left[i] - out.left[i - 1])));
  }
  return step;
}

double meanGain(nlohmann::json params)
{
  const auto out = render(params, std::vector<float>(4 * 48000, 0.5f));
  double sum = 0.0;
  for (size_t i = 48000; i < out.left.size(); ++i) sum += out.left[i];
  return sum / (out.left.size() - 48000) / 0.5;
}

void verifyPatternTremSmoothAndSwing()
{
  auto params = defaults("pattern_trem");
  params["depth"] = 1.0f;
  params["p1"] = 1.5f / 16.0f; // pattern 2: every other 16th on
  params["p3"] = 0.0f;
  const double hard = largestGainStep(params);
  params["p3"] = 1.0f;
  const double soft = largestGainStep(params);
  require(soft * 5.0 < hard, "pattern trem Smooth must soften the edges, steps " + fmt(hard) +
                                 " and " + fmt(soft));

  params["p3"] = 0.35f;
  params["p4"] = 0.0f;
  const double straight = meanGain(params);
  params["p4"] = 1.0f;
  const double swung = meanGain(params);
  // Swing lengthens the on-beat 16th of each pair from 50 % to 75 %.
  require(swung > straight + 0.15, "pattern trem Swing must lengthen the on-beat steps, duty " +
                                       fmt(straight) + " and " + fmt(swung));
}

double levelOfTone(nlohmann::json params, double hz)
{
  const auto out = render(params, sine(hz, 0.3f, 3 * 48000));
  double sum = 0.0;
  for (size_t i = 48000; i < out.left.size(); ++i) sum += static_cast<double>(out.left[i]) * out.left[i];
  return db(std::sqrt(sum / (out.left.size() - 48000)) / (0.3 / std::sqrt(2.0)));
}

void verifyRotaryBalanceAndSpread()
{
  auto params = defaults("rotary");
  params["p3"] = 0.5f;
  const double hornCentre = levelOfTone(params, 4000.0);
  const double drumCentre = levelOfTone(params, 120.0);
  params["p3"] = 0.0f;
  require(levelOfTone(params, 4000.0) < hornCentre - 20.0, "rotary Balance at 0 must mute the horn");
  params["p3"] = 1.0f;
  require(levelOfTone(params, 120.0) < drumCentre - 20.0, "rotary Balance at 1 must mute the drum");

  auto close = defaults("rotary");
  close["p4"] = 0.0f;
  auto wide = close;
  wide["p4"] = 1.0f;
  require(maxDifference(close, wide) > 1e-3, "rotary Mic Spread must change the sound");
}

} // namespace

int main()
{
  const std::pair<const char*, void (*)()> checks[] = {
    {"descriptor indexes", verifyDescriptorIndexesAreStable},
    {"missing controls", verifyMissingControlsFallBackToDefaults},
    {"flanger manual", verifyFlangerManualMovesTheSweep},
    {"stereo controls", verifyStereoControls},
    {"phaser polarity", verifyPhaserPolarity},
    {"vintage trem pan", verifyVintageTremPans},
    {"pattern trem smooth and swing", verifyPatternTremSmoothAndSwing},
    {"rotary balance and spread", verifyRotaryBalanceAndSpread},
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
