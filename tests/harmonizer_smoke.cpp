#include "daisyfx/hosted/modes/harmonizer_mode.h"
#include "daisyfx/hosted/dsp/pitch_tracker.h"

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

float midiToHz(int note) { return 440.0f * std::pow(2.0f, (note - 69) / 12.0f); }

// Hann-windowed DFT bin.
double toneMagnitude(const std::vector<float>& samples, double frequency)
{
  double real = 0.0, imaginary = 0.0, weight = 0.0;
  const std::size_t count = samples.size();
  for (std::size_t i = 0; i < count; ++i) {
    const double window = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) /
                                               static_cast<double>(count - 1));
    const double phase = 2.0 * M_PI * frequency * static_cast<double>(i) / kRate;
    real += samples[i] * window * std::cos(phase);
    imaginary -= samples[i] * window * std::sin(phase);
    weight += window;
  }
  return 2.0 * std::sqrt(real * real + imaginary * imaginary) / weight;
}

// Renders a sustained guitar-like note through the mode and returns the tail.
std::vector<float> render(int midiNote, float interval, float key, float scale)
{
  pedal::HarmonizerMode mode;
  mode.Init();
  auto params = pedal::mod_fx::ParamSet::make_default();
  params.p1 = interval;
  params.p2 = key;
  params.depth = scale;
  params.speed = 10.0f;   // fastest tracking, so the harmony settles quickly
  params.tone = 0.5f;
  mode.Prepare(params);

  const float f0 = midiToHz(midiNote);
  std::vector<float> out;
  out.reserve(48000);
  for (int n = 0; n < 96000; ++n) {
    if ((n % 48) == 0) mode.Prepare(params);
    const float phase = kTwoPi * f0 * static_cast<float>(n) / kRate;
    const float x = 0.5f * (0.7f * std::sin(phase) + 0.2f * std::sin(2.0f * phase) +
                            0.1f * std::sin(3.0f * phase));
    const auto sample = mode.Process({x, x}, params);
    if (n >= 48000) out.push_back(sample.left);
  }
  return out;
}

// The point of the mode: the same "third up" must come out major on some
// degrees of the key and minor on others. A fixed-interval shifter would put
// every note the same distance away.
void verifyThirdsFollowTheKey()
{
  // Interval index 5 of 10 is "3rd up"; key index 0 is C; scale index 0 is major.
  const float thirdUp = (5.0f + 0.5f) / 10.0f;
  const float keyC = (0.0f + 0.5f) / 12.0f;
  const float major = (0.0f + 0.5f) / 5.0f;

  struct Case { int note; const char* name; int expected; int wrong; };
  const Case cases[] = {
      {60, "C", 4, 3},   // major third up, not minor
      {62, "D", 3, 4},   // minor third up, not major
      {64, "E", 3, 4},
      {65, "F", 4, 3},
      {67, "G", 4, 3},
      {69, "A", 3, 4},
  };

  for (const auto& item : cases) {
    const auto rendered = render(item.note, thirdUp, keyC, major);
    const double atExpected = toneMagnitude(rendered, midiToHz(item.note + item.expected));
    const double atWrong = toneMagnitude(rendered, midiToHz(item.note + item.wrong));
    require(atExpected > atWrong * 3.0,
            std::string("harmony above ") + item.name + " must be " +
                std::to_string(item.expected) + " semitones, not " +
                std::to_string(item.wrong) + " (got " + std::to_string(atExpected) +
                " vs " + std::to_string(atWrong) + ")");
  }
}

// The dry note must survive; this is a harmoniser, not a pitch shifter.
void verifyDrySurvives()
{
  const auto rendered = render(64, (5.0f + 0.5f) / 10.0f, (0.0f + 0.5f) / 12.0f, (0.0f + 0.5f) / 5.0f);
  const double atRoot = toneMagnitude(rendered, midiToHz(64));
  require(atRoot > 0.05, "the played note must remain present alongside the harmony");
}

// Changing the scale must change which harmony note comes out.
void verifyScaleChangesTheHarmony()
{
  const float thirdUp = (5.0f + 0.5f) / 10.0f;
  const float keyC = (0.0f + 0.5f) / 12.0f;
  const float major = (0.0f + 0.5f) / 5.0f;
  const float minor = (1.0f + 0.5f) / 5.0f;

  // On the root of the key, a major scale gives a major third and a natural
  // minor scale gives a minor third.
  const auto inMajor = render(60, thirdUp, keyC, major);
  const auto inMinor = render(60, thirdUp, keyC, minor);
  require(toneMagnitude(inMajor, midiToHz(64)) > toneMagnitude(inMajor, midiToHz(63)) * 3.0,
          "C in C major must harmonise to E");
  require(toneMagnitude(inMinor, midiToHz(63)) > toneMagnitude(inMinor, midiToHz(64)) * 3.0,
          "C in C minor must harmonise to Eb");
}

// The tracker underneath must find the right note across the guitar range,
// because a wrong note here is a wrong harmony.
void verifyTrackerAccuracy()
{
  for (const float f0 : {82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f, 440.0f, 659.3f}) {
    pedal::PitchTracker tracker;
    tracker.Init(kRate);
    for (int n = 0; n < 24000; ++n) {
      const float phase = kTwoPi * f0 * static_cast<float>(n) / kRate;
      tracker.Push(0.5f * (0.6f * std::sin(phase) + 0.3f * std::sin(2.0f * phase) +
                           0.15f * std::sin(3.0f * phase)));
    }
    require(tracker.Voiced(), "tracker must lock onto a sustained note");
    const double cents = 1200.0 * std::log2(tracker.FrequencyHz() / f0);
    require(std::fabs(cents) < 25.0,
            "tracker must stay within a quarter tone at " + std::to_string(f0) +
                " Hz; got " + std::to_string(cents) + " cents");
  }
}

// Silence must not be reported as a note.
void verifyTrackerRejectsSilence()
{
  pedal::PitchTracker tracker;
  tracker.Init(kRate);
  for (int n = 0; n < 24000; ++n) tracker.Push(0.0f);
  require(!tracker.Voiced(), "silence must not be reported as a tracked note");
}

} // namespace

int main()
{
  verifyTrackerAccuracy();
  verifyTrackerRejectsSilence();
  verifyThirdsFollowTheKey();
  verifyDrySurvives();
  verifyScaleChangesTheHarmony();
  std::printf("harmonizer smoke passed\n");
  return 0;
}
