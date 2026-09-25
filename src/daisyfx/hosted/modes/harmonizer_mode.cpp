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

// Pan of the two voices when both sound: equal power, each about a third of
// the way to its side. The gains carry sqrt(2) so each voice keeps the power
// it has when centred at unity in both channels.
constexpr float kPanNear = 1.3066f;   // sqrt(2) * cos(pi/8)
constexpr float kPanFar  = 0.5412f;   // sqrt(2) * sin(pi/8)
// Envelope time for choosing the tracker's source.
constexpr float kSourceSmoothing = 0.0004f;

void HarmonizerMode::Init()
{
    for (auto& voice : voices_) {
        for (auto& channel : voice.channels) {
            channel.shifter.Init(channel.buf, kBufSize, SAMPLE_RATE, kGrainSize);
            // Loudness-neutral: the voice feeds no loop.
            channel.tone.Init(SAMPLE_RATE, ToneGain::Loudness);
        }
    }
    tracker_.Init(SAMPLE_RATE);
    Reset();
}

void HarmonizerMode::Reset()
{
    for (auto& voice : voices_) {
        for (auto& channel : voice.channels) {
            channel.shifter.Reset();
            channel.tone.Reset();
            channel.dc.Init();
        }
        voice.semitones = 0.0f;
        voice.semitoneTarget = 0.0f;
        voice.ratio = 1.0f;
        voice.ratioStep = 0.0f;
    }
    tracker_.Reset();
    env_mid_ = 0.0f;
    env_side_ = 0.0f;
    lastNote_ = -1;
    seeded_ = false;
}

float HarmonizerMode::semitonesForNote(int midiNote, int interval) const
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

    const int targetIndex = degree + kIntervalDegrees[interval];
    const int targetOctave = octave + floorDiv(targetIndex, SCALE_DEGREES);
    const int targetDegree = positiveMod(targetIndex, SCALE_DEGREES);

    const int playedSemitone = octave * 12 + scale[degree];
    const int targetSemitone = targetOctave * 12 + scale[targetDegree];
    return static_cast<float>(targetSemitone - playedSemitone);
}

void HarmonizerMode::PrepareVoice(Voice& voice, float tone)
{
    if (lastNote_ >= 0) voice.semitoneTarget = semitonesForNote(lastNote_, voice.interval);
    if (!seeded_) {
        voice.semitones = voice.semitoneTarget;
        voice.ratio = std::pow(2.0f, voice.semitones / 12.0f);
    }
    voice.semitones += glide_ * (voice.semitoneTarget - voice.semitones);
    const float targetRatio = std::pow(2.0f, voice.semitones / 12.0f);
    voice.ratioStep = (targetRatio - voice.ratio) / static_cast<float>(BLOCK_SIZE);
    for (auto& channel : voice.channels) {
        channel.shifter.SetShift(voice.semitones);
        channel.tone.SetKnob(tone);
    }
}

void HarmonizerMode::Prepare(const ParamSet& params)
{
    voices_[0].interval = std::clamp(
        static_cast<int>(params.p1 * static_cast<float>(INTERVAL_COUNT)), 0, INTERVAL_COUNT - 1);
    key_ = std::clamp(
        static_cast<int>(params.p2 * static_cast<float>(KEY_COUNT)), 0, KEY_COUNT - 1);
    scale_ = std::clamp(
        static_cast<int>(params.depth * static_cast<float>(SCALE_COUNT)), 0, SCALE_COUNT - 1);

    // Interval 2 (p3): Off, then the same ten intervals. Voice 2 Level (p4)
    // sets it against the first voice, whose level is the Mix control.
    const int second = std::clamp(
        static_cast<int>(params.p3 * static_cast<float>(INTERVAL_COUNT + 1)), 0, INTERVAL_COUNT);
    voices_[1].active = second > 0;
    voices_[1].interval = second > 0 ? second - 1 : 0;
    voices_[1].level = params.p4;

    // Two voices spread apart; one voice stays centred, as it always was.
    if (voices_[1].active) {
        voices_[0].gain[0] = kPanNear;
        voices_[0].gain[1] = kPanFar;
        voices_[1].gain[0] = kPanFar;
        voices_[1].gain[1] = kPanNear;
    } else {
        voices_[0].gain[0] = 1.0f;
        voices_[0].gain[1] = 1.0f;
    }

    // Speed is presented as Tracking: how quickly the voice moves to a new note.
    const float speed = std::clamp((params.speed - 0.05f) / 9.95f, 0.0f, 1.0f);
    glide_ = 0.05f + speed * 0.90f;

    if (tracker_.Voiced()) {
        const float hz = tracker_.FrequencyHz();
        if (hz > 20.0f) {
            // Hysteresis: keep the current note until the pitch is clearly on
            // another one. Rounding alone flipped the harmony every time
            // vibrato or a bend crossed a semitone boundary, 29 times in 3 s
            // of +/-20 cents of vibrato.
            const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
            if (lastNote_ < 0 || std::fabs(midi - static_cast<float>(lastNote_)) > kNoteHysteresis) {
                lastNote_ = static_cast<int>(std::lround(midi));
            }
        }
    }
    // When nothing is being tracked the previous interval is held, so a
    // decaying string does not drag the harmony somewhere else.

    PrepareVoice(voices_[0], params.tone);
    if (voices_[1].active) PrepareVoice(voices_[1], params.tone);
    seeded_ = true;
}

StereoFrame HarmonizerMode::Process(StereoFrame input, const ParamSet& params)
{
    (void)params;
    const float mid = 0.5f * (input.left + input.right);
    const float side = 0.5f * (input.left - input.right);
    env_mid_ += kSourceSmoothing * (std::fabs(mid) - env_mid_);
    env_side_ += kSourceSmoothing * (std::fabs(side) - env_side_);
    tracker_.Push(env_side_ > env_mid_ ? side : mid);

    // The harmony sits against the dry note; the engine applies its own dry/wet
    // mix on top of this.
    StereoFrame out{input.left, input.right};
    const float in[2] = {input.left, input.right};
    for (auto& voice : voices_) {
        if (!voice.active) continue;
        voice.ratio += voice.ratioStep;
        float wet[2];
        for (int c = 0; c < 2; ++c) {
            auto& channel = voice.channels[c];
            channel.shifter.SetRatioFast(voice.ratio);
            wet[c] = channel.dc.Process(channel.tone.Process(channel.shifter.Process(in[c])));
        }
        out.left  += wet[0] * voice.gain[0] * voice.level;
        out.right += wet[1] * voice.gain[1] * voice.level;
    }
    return out;
}

} // namespace pedal
