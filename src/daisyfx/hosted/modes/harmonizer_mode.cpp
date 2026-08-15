#include "harmonizer_mode.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

namespace {

// Semitone offsets from the root. Keep in step with the scale names in
// DaisyFxCatalog.cpp and daisyValues.ts.
constexpr int kScales[HarmonizerMode::SCALE_COUNT][HarmonizerMode::SCALE_DEGREES] = {
    {0, 2, 4, 5, 7, 9, 11},   // Major
    {0, 2, 3, 5, 7, 8, 10},   // Minor
    {0, 2, 3, 5, 7, 9, 10},   // Dorian
    {0, 2, 4, 5, 7, 9, 10},   // Mixolydian
    {0, 2, 3, 5, 7, 8, 11},   // Harmonic minor
};

// Offsets in scale degrees, not semitones — that is the whole point of the
// mode. A third is two degrees up whether it lands on a major or a minor one.
constexpr int kIntervalDegrees[HarmonizerMode::INTERVAL_COUNT] = {
    -7,  // Oct down
    -5,  // 6th down
    -4,  // 5th down
    -3,  // 4th down
    -2,  // 3rd down
     2,  // 3rd up
     3,  // 4th up
     4,  // 5th up
     5,  // 6th up
     7,  // Oct up
};

int floorDiv(int a, int b)
{
    const int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int positiveMod(int a, int b)
{
    const int m = a % b;
    return m < 0 ? m + b : m;
}

} // namespace

void HarmonizerMode::Init()
{
    shifter_.Init(buf_, kBufSize, SAMPLE_RATE, kGrainSize);
    tracker_.Init(SAMPLE_RATE);
    tone_.Init();
    Reset();
}

void HarmonizerMode::Reset()
{
    shifter_.Reset();
    tracker_.Reset();
    tone_.Reset();
    dc_.Init();
    lastNote_ = -1;
    semitones_ = 0.0f;
    semitoneTarget_ = 0.0f;
    ratio_ = 1.0f;
    ratioStep_ = 0.0f;
    seeded_ = false;
}

float HarmonizerMode::semitonesForNote(int midiNote) const
{
    const int* scale = kScales[scale_];

    // Where the played note sits relative to the key, as an octave plus a
    // pitch class.
    const int fromRoot = midiNote - key_;
    const int octave = floorDiv(fromRoot, 12);
    const int pitchClass = positiveMod(fromRoot, 12);

    // Nearest scale degree. A note outside the key — a passing tone, or a bend
    // caught mid-flight — is treated as the closest degree rather than
    // abandoning the harmony.
    int degree = 0;
    int bestDistance = 128;
    for (int d = 0; d < SCALE_DEGREES; ++d) {
        const int distance = std::abs(scale[d] - pitchClass);
        if (distance < bestDistance) {
            bestDistance = distance;
            degree = d;
        }
    }

    const int targetIndex = degree + kIntervalDegrees[interval_];
    const int targetOctave = octave + floorDiv(targetIndex, SCALE_DEGREES);
    const int targetDegree = positiveMod(targetIndex, SCALE_DEGREES);

    const int playedSemitone = octave * 12 + scale[degree];
    const int targetSemitone = targetOctave * 12 + scale[targetDegree];
    return static_cast<float>(targetSemitone - playedSemitone);
}

void HarmonizerMode::Prepare(const ParamSet& params)
{
    interval_ = std::clamp(
        static_cast<int>(params.p1 * static_cast<float>(INTERVAL_COUNT)), 0, INTERVAL_COUNT - 1);
    key_ = std::clamp(
        static_cast<int>(params.p2 * static_cast<float>(KEY_COUNT)), 0, KEY_COUNT - 1);
    scale_ = std::clamp(
        static_cast<int>(params.depth * static_cast<float>(SCALE_COUNT)), 0, SCALE_COUNT - 1);

    // Speed is presented as Tracking: how quickly the voice moves to a new note.
    const float speed = std::clamp((params.speed - 0.05f) / 9.95f, 0.0f, 1.0f);
    glide_ = 0.05f + speed * 0.90f;

    if (tracker_.Voiced()) {
        const float hz = tracker_.FrequencyHz();
        if (hz > 20.0f) {
            const int note = static_cast<int>(
                std::lround(69.0f + 12.0f * std::log2(hz / 440.0f)));
            // Recompute only when the note or the harmony settings change; the
            // interval is a property of the note, not of every control block.
            if (note != lastNote_) {
                lastNote_ = note;
            }
            semitoneTarget_ = semitonesForNote(note);
        }
    }
    // When nothing is being tracked the previous interval is held, so a
    // decaying string does not drag the harmony somewhere else.

    if (!seeded_) {
        seeded_ = true;
        semitones_ = semitoneTarget_;
        ratio_ = std::pow(2.0f, semitones_ / 12.0f);
    }

    semitones_ += glide_ * (semitoneTarget_ - semitones_);
    const float targetRatio = std::pow(2.0f, semitones_ / 12.0f);

    shifter_.SetShift(semitones_);
    ratioStep_ = (targetRatio - ratio_) / static_cast<float>(BLOCK_SIZE);

    tone_.SetKnob(params.tone);
}

StereoFrame HarmonizerMode::Process(StereoFrame input, const ParamSet& params)
{
    const float mono = input.mono();
    tracker_.Push(mono);

    ratio_ += ratioStep_;
    shifter_.SetRatioFast(ratio_);

    const float voice = dc_.Process(tone_.Process(shifter_.Process(mono)));

    // The harmony sits against the dry note; the engine applies its own dry/wet
    // mix on top of this.
    const float out = mono + voice;
    return {out, out};
}

} // namespace pedal
