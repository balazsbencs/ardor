#include "dsp/StereoWidenerProcessor.h"

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

struct Rendered {
  double leftEnergy = 0.0;
  double rightEnergy = 0.0;
  double monoEnergy = 0.0;   // energy of (L+R)/2, the fold-down
  double sideEnergy = 0.0;
};

// Drives a stereo signal whose two channels carry different content, so width
// changes are visible.
Rendered render(ardor::StereoWidenerProcessor& widener, int frames)
{
  Rendered out;
  for (int n = 0; n < frames; ++n) {
    const float t = static_cast<float>(n) / kRate;
    const float left = 0.4f * std::sin(kTwoPi * 220.0f * t);
    const float right = 0.4f * std::sin(kTwoPi * 330.0f * t);
    const auto sample = widener.process({left, right});
    if (n < frames / 4) continue;   // let the smoothers settle
    out.leftEnergy += static_cast<double>(sample.left) * sample.left;
    out.rightEnergy += static_cast<double>(sample.right) * sample.right;
    const double mono = 0.5 * (static_cast<double>(sample.left) + sample.right);
    const double side = 0.5 * (static_cast<double>(sample.left) - sample.right);
    out.monoEnergy += mono * mono;
    out.sideEnergy += side * side;
  }
  return out;
}

ardor::StereoWidenerProcessor make(float width, float delayMs, float bassHz)
{
  ardor::StereoWidenerProcessor widener;
  std::string error;
  if (!widener.prepare(kRate, error)) throw std::runtime_error(error);
  widener.setWidth(width);
  widener.setDelayMs(delayMs);
  widener.setBassMonoHz(bassHz);
  widener.setLevelDb(0.0f);
  widener.reset();
  return widener;
}

// Width 1 must be a true bypass, not merely close to one.
void verifyUnityIsTransparent()
{
  auto widener = make(1.0f, 0.0f, 0.0f);
  for (int n = 0; n < 4096; ++n) {
    const float t = static_cast<float>(n) / kRate;
    const float left = 0.4f * std::sin(kTwoPi * 220.0f * t);
    const float right = 0.4f * std::sin(kTwoPi * 330.0f * t);
    const auto out = widener.process({left, right});
    require(std::fabs(out.left - left) < 1.0e-5f && std::fabs(out.right - right) < 1.0e-5f,
            "width 1 with no delay and no bass mono must pass audio through unchanged");
  }
}

void verifyWidthScalesTheSide()
{
  auto narrow = make(0.0f, 0.0f, 0.0f);
  auto normal = make(1.0f, 0.0f, 0.0f);
  auto wide = make(2.0f, 0.0f, 0.0f);

  const auto atZero = render(narrow, 48000);
  const auto atOne = render(normal, 48000);
  const auto atTwo = render(wide, 48000);

  require(atZero.sideEnergy < atOne.sideEnergy * 0.01,
          "width 0 must collapse the image to mono");
  require(atTwo.sideEnergy > atOne.sideEnergy * 3.0,
          "width 2 must roughly double the side component");
}

// The point of scaling the side rather than the channels: a mono fold-down must
// not care what width is set to.
void verifyMonoFoldDownIsUnaffectedByWidth()
{
  auto narrow = make(0.0f, 0.0f, 0.0f);
  auto wide = make(2.0f, 0.0f, 0.0f);
  const auto atZero = render(narrow, 48000);
  const auto atTwo = render(wide, 48000);

  const double ratio = atTwo.monoEnergy / (atZero.monoEnergy + 1.0e-12);
  require(ratio > 0.99 && ratio < 1.01,
          "the mono fold-down must be identical at every width; ratio " +
              std::to_string(ratio));
}

// Delaying the side rather than a channel is what keeps that true.
void verifyDelayDoesNotDamageTheMonoFoldDown()
{
  auto without = make(1.0f, 0.0f, 0.0f);
  auto with = make(1.0f, 20.0f, 0.0f);
  const auto dry = render(without, 48000);
  const auto delayed = render(with, 48000);

  const double ratio = delayed.monoEnergy / (dry.monoEnergy + 1.0e-12);
  require(ratio > 0.99 && ratio < 1.01,
          "a side delay must leave the mono fold-down untouched; ratio " +
              std::to_string(ratio));
  require(delayed.sideEnergy > 0.0, "the side must survive the delay");
}

// Bass mono must pull the low end to the centre and leave the top alone.
void verifyBassMonoCentresTheLowEnd()
{
  const auto sideEnergyAt = [](float frequency, float bassHz) {
    auto widener = make(1.5f, 0.0f, bassHz);
    double side = 0.0;
    for (int n = 0; n < 48000; ++n) {
      const float t = static_cast<float>(n) / kRate;
      // Hard-panned tone: all side, no mid.
      const float x = 0.4f * std::sin(kTwoPi * frequency * t);
      const auto out = widener.process({x, -x});
      if (n < 12000) continue;
      const double s = 0.5 * (static_cast<double>(out.left) - out.right);
      side += s * s;
    }
    return side;
  };

  const double lowOff = sideEnergyAt(60.0f, 0.0f);
  const double lowOn = sideEnergyAt(60.0f, 200.0f);
  const double highOff = sideEnergyAt(2000.0f, 0.0f);
  const double highOn = sideEnergyAt(2000.0f, 200.0f);

  require(lowOn < lowOff * 0.2, "bass mono must centre content below its corner");
  require(highOn > highOff * 0.8, "bass mono must leave content above its corner alone");
}

void verifyRejectsBadRate()
{
  ardor::StereoWidenerProcessor widener;
  std::string error;
  require(!widener.prepare(0.0f, error), "a non-positive sample rate must be rejected");
  require(!error.empty(), "rejection must explain itself");
}

} // namespace

int main()
{
  verifyUnityIsTransparent();
  verifyWidthScalesTheSide();
  verifyMonoFoldDownIsUnaffectedByWidth();
  verifyDelayDoesNotDamageTheMonoFoldDown();
  verifyBassMonoCentresTheLowEnd();
  verifyRejectsBadRate();
  std::printf("stereo widener smoke passed\n");
  return 0;
}
