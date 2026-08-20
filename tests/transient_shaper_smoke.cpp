#include "dynamics/TransientShaperProcessor.h"

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

constexpr float kRate = 48000.0f;
constexpr float kTwoPi = 6.28318530718f;

// A plucked note: an instant rise followed by a long decay.
std::vector<float> pluckedNote(float seconds, float decaySeconds, float amplitude)
{
  std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
  for (std::size_t n = 0; n < out.size(); ++n) {
    const float t = static_cast<float>(n) / kRate;
    out[n] = amplitude * std::exp(-t / decaySeconds) * std::sin(kTwoPi * 196.0f * t);
  }
  return out;
}

std::vector<float> heldTone(float seconds, float frequency)
{
  std::vector<float> out(static_cast<std::size_t>(seconds * kRate));
  for (std::size_t n = 0; n < out.size(); ++n) {
    out[n] = 0.4f * std::sin(kTwoPi * frequency * static_cast<float>(n) / kRate);
  }
  return out;
}

struct Shape {
  double onsetPeak = 0.0;    // peak within the first 20 ms
  double tailEnergy = 0.0;   // energy from 500 ms onwards
  double gainRippleDb = 0.0; // gain swing over the second half
};

Shape render(float attack, float sustain, const std::vector<float>& note)
{
  ardor::TransientShaperProcessor shaper;
  std::string error;
  nlohmann::json params;
  params["attack"] = attack;
  params["sustain"] = sustain;
  params["output_db"] = 0.0f;
  params["mix"] = 1.0f;
  if (!shaper.configure(params, kRate, error)) throw std::runtime_error(error);
  shaper.reset();

  double lowestGainDb = 1.0e9;
  double highestGainDb = -1.0e9;
  Shape shape;
  for (std::size_t n = 0; n < note.size(); ++n) {
    const float x = note[n];
    const auto out = shaper.process({x, x});
    const double t = static_cast<double>(n) / kRate;
    if (t < 0.020) shape.onsetPeak = std::max(shape.onsetPeak, std::fabs(static_cast<double>(out.left)));
    if (t >= 0.500) shape.tailEnergy += static_cast<double>(out.left) * out.left;
    if (n >= note.size() / 2) {
      lowestGainDb = std::min(lowestGainDb, static_cast<double>(shaper.currentGainDb()));
      highestGainDb = std::max(highestGainDb, static_cast<double>(shaper.currentGainDb()));
    }
  }
  shape.gainRippleDb = highestGainDb - lowestGainDb;
  return shape;
}

// Attack up must make the pick louder; attack down must soften it. Neither may
// touch the decay — that is what the sustain control is for.
void verifyAttackShapesOnlyTheOnset()
{
  const auto note = pluckedNote(1.5f, 0.6f, 0.5f);
  const auto flat = render(0.0f, 0.0f, note);
  const auto sharp = render(100.0f, 0.0f, note);
  const auto soft = render(-100.0f, 0.0f, note);

  require(sharp.onsetPeak > flat.onsetPeak * 2.0, "attack up must emphasise the onset");
  require(soft.onsetPeak < flat.onsetPeak * 0.8, "attack down must soften the onset");
  require(std::fabs(sharp.tailEnergy / flat.tailEnergy - 1.0) < 0.01
            && std::fabs(soft.tailEnergy / flat.tailEnergy - 1.0) < 0.01,
          "the attack control must leave the decay alone");
}

// And the mirror: sustain must act on the decay and leave the pick alone.
void verifySustainShapesOnlyTheTail()
{
  const auto note = pluckedNote(1.5f, 0.6f, 0.5f);
  const auto flat = render(0.0f, 0.0f, note);
  const auto longer = render(0.0f, 100.0f, note);
  const auto shorter = render(0.0f, -100.0f, note);

  require(longer.tailEnergy > flat.tailEnergy * 2.0, "sustain up must hold the tail");
  require(shorter.tailEnergy < flat.tailEnergy * 0.8, "sustain down must cut the tail short");
  require(std::fabs(longer.onsetPeak - flat.onsetPeak) < 0.01 * flat.onsetPeak
            && std::fabs(shorter.onsetPeak - flat.onsetPeak) < 0.01 * flat.onsetPeak,
          "the sustain control must leave the pick alone");
}

// Both at zero must be a bypass. A shaper that colours at its neutral setting is
// useless as a comparison reference.
void verifyNeutralIsTransparent()
{
  ardor::TransientShaperProcessor shaper;
  std::string error;
  nlohmann::json params;
  params["attack"] = 0.0f;
  params["sustain"] = 0.0f;
  require(shaper.configure(params, kRate, error), error);
  shaper.reset();

  for (const float x : pluckedNote(0.5f, 0.4f, 0.5f)) {
    const auto out = shaper.process({x, -x});
    require(std::fabs(out.left - x) < 1.0e-4f && std::fabs(out.right + x) < 1.0e-4f,
            "a neutral setting must pass audio through unchanged");
  }
}

// The whole point of measuring the level against a smoothed copy of itself: a
// held note has no transient, so the gain must sit still. A detector built from
// two followers with different time constants fails this — it reads the ripple
// of the waveform as a permanent transient and shapes every note.
void verifyHeldNotesAreLeftAlone()
{
  for (const float frequency : {82.4f, 196.0f, 440.0f}) {
    const auto tone = heldTone(2.0f, frequency);
    const auto sharp = render(100.0f, 0.0f, tone);
    const auto longer = render(0.0f, 100.0f, tone);
    require(sharp.gainRippleDb < 0.5 && longer.gainRippleDb < 0.5,
            "a held tone must not be shaped; at " + std::to_string(frequency) + " Hz the gain moved "
              + std::to_string(std::max(sharp.gainRippleDb, longer.gainRippleDb)) + " dB");
  }
}

// The effect must depend on the shape of the envelope, not on how loud the
// playing is — that is what separates this from a compressor.
void verifyResponseIsLevelIndependent()
{
  const auto loud = pluckedNote(1.0f, 0.5f, 0.5f);
  const auto quiet = pluckedNote(1.0f, 0.5f, 0.05f);   // 20 dB down

  const double loudRatio = render(100.0f, 0.0f, loud).onsetPeak / render(0.0f, 0.0f, loud).onsetPeak;
  const double quietRatio = render(100.0f, 0.0f, quiet).onsetPeak / render(0.0f, 0.0f, quiet).onsetPeak;
  require(std::fabs(loudRatio - quietRatio) / loudRatio < 0.05,
          "the same setting must shape a quiet note like a loud one; ratios "
            + std::to_string(loudRatio) + " vs " + std::to_string(quietRatio));
}

// Sustain boost must not turn the noise floor up between notes.
void verifySilenceStaysSilent()
{
  ardor::TransientShaperProcessor shaper;
  std::string error;
  nlohmann::json params;
  params["attack"] = 100.0f;
  params["sustain"] = 100.0f;
  require(shaper.configure(params, kRate, error), error);
  shaper.reset();

  // A note, then nothing. The gain must come back to unity rather than climb.
  for (const float x : pluckedNote(0.5f, 0.2f, 0.5f)) shaper.process({x, x});
  for (int n = 0; n < static_cast<int>(kRate); ++n) shaper.process({0.0f, 0.0f});
  require(std::fabs(shaper.currentGainDb()) < 0.1f,
          "silence must not be boosted; the gain settled at "
            + std::to_string(shaper.currentGainDb()) + " dB");
}

// One detector across both channels, so a transient on one side cannot pull the
// image over.
void verifyImageIsStable()
{
  ardor::TransientShaperProcessor shaper;
  std::string error;
  nlohmann::json params;
  params["attack"] = 100.0f;
  require(shaper.configure(params, kRate, error), error);
  shaper.reset();

  for (const float x : pluckedNote(0.5f, 0.3f, 0.5f)) {
    const auto out = shaper.process({x, 0.0f});
    require(std::fabs(out.right) < 1.0e-6f,
            "a silent channel must stay silent whatever the other one does");
  }
}

void verifyRejectsBadRate()
{
  ardor::TransientShaperProcessor shaper;
  std::string error;
  require(!shaper.configure(nlohmann::json::object(), 0.0f, error),
          "a non-positive sample rate must be rejected");
  require(!error.empty(), "rejection must explain itself");
}

} // namespace

int main()
{
  verifyNeutralIsTransparent();
  verifyAttackShapesOnlyTheOnset();
  verifySustainShapesOnlyTheTail();
  verifyHeldNotesAreLeftAlone();
  verifyResponseIsLevelIndependent();
  verifySilenceStaysSilent();
  verifyImageIsStable();
  verifyRejectsBadRate();
  std::printf("transient shaper smoke passed\n");
  return 0;
}
