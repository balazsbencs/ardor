#include "whammy_mode.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

namespace {

struct Preset {
    float heel;      // pedal at 0
    float toe;       // pedal at 1
    bool  harmony;   // true = keep the dry note under the shifted voice
};

// One selector covering both families, as the original unit has. Entries 0-9
// are the Whammy modes: heel is unison, toe is the named interval, and no dry
// signal is carried. Entries 10-18 are the Harmony modes: the pedal morphs
// between two fixed chromatic intervals over the top of the dry note. Heel is
// the "toe up" reading on the original, toe the "toe down" one.
//
// Keep this in step with kPresetNames in DaisyFxCatalog.cpp.
constexpr Preset kPresets[WhammyMode::PRESET_COUNT] = {
    {0.0f,  24.0f, false},   // 2 OCT UP
    {0.0f,  12.0f, false},   // 1 OCT UP
    {0.0f,   7.0f, false},   // 5TH UP
    {0.0f,   5.0f, false},   // 4TH UP
    {0.0f,  -2.0f, false},   // 2ND DN
    {0.0f,  -5.0f, false},   // 4TH DN
    {0.0f,  -7.0f, false},   // 5TH DN
    {0.0f, -12.0f, false},   // 1 OCT DN
    {0.0f, -24.0f, false},   // 2 OCT DN
    {0.0f, -36.0f, false},   // DIVE BOMB

    {-12.0f,  12.0f, true},  // OCT DN / OCT UP
    { -7.0f,  -5.0f, true},  // 5TH DN / 4TH DN
    { -5.0f,  -3.0f, true},  // 4TH DN / 3RD DN
    {  7.0f,  10.0f, true},  // 5TH UP / 7TH UP
    {  7.0f,   9.0f, true},  // 5TH UP / 6TH UP
    {  5.0f,   7.0f, true},  // 4TH UP / 5TH UP
    {  4.0f,   5.0f, true},  // 3RD UP / 4TH UP
    {  3.0f,   4.0f, true},  // MIN 3RD UP / 3RD UP
    {  2.0f,   4.0f, true},  // 2ND UP / 3RD UP
};

// Detune modes, as on the original: a close copy under the note. Shallow is a
// subtle doubling, Deep a wide chorus-like spread.
constexpr float kShallowCents = 8.0f;
constexpr float kDeepCents    = 20.0f;

} // namespace

void WhammyMode::Init() {
    for (auto& voice : voices_) {
        voice.shifter.Init(voice.buf, kBufSize, SAMPLE_RATE, kGrainSize);
        // Loudness-neutral: the voice feeds no loop, so Tone changes colour,
        // not level. The loop-safe gain cost up to 7.5 dB at the bright end.
        voice.tone.Init(SAMPLE_RATE, ToneGain::Loudness);
    }
    Reset();
}

void WhammyMode::Reset() {
    for (auto& voice : voices_) {
        voice.shifter.Reset();
        voice.tone.Reset();
        voice.dc.Init();
    }
    detune_          = 0.0f;
    spread_ratio_    = 1.0f;
    semitones_       = 0.0f;
    semitone_target_ = 0.0f;
    ratio_           = 1.0f;
    ratio_step_      = 0.0f;
    harmony_         = false;
    harmony_level_   = 1.0f;
    preset_          = 0;
    seeded_          = false;
}

void WhammyMode::Prepare(const ParamSet& params) {
    // One selector across both families; the family follows the chosen preset.
    preset_ = std::clamp(
        static_cast<int>(params.p2 * static_cast<float>(PRESET_COUNT)), 0, PRESET_COUNT - 1);

    const Preset preset = kPresets[preset_];
    harmony_ = preset.harmony;

    const float pedal = std::clamp(params.p1, 0.0f, 1.0f);
    semitone_target_ = preset.heel + (preset.toe - preset.heel) * pedal;

    // Detune (p3): Off / Shallow / Deep. It overrides the preset and always
    // carries the dry note. The L voice goes down and the R voice up by the
    // same amount, which the ratio spread below expresses.
    const int detune = std::clamp(static_cast<int>(params.p3 * 2.999f), 0, 2);
    detune_ = detune == 0 ? 0.0f
            : (detune == 1 ? kShallowCents : kDeepCents) * (0.5f + pedal) / 100.0f;
    if (detune_ > 0.0f) {
        harmony_ = true;
        semitone_target_ = -detune_;
    }

    // Level of the shifted voice against the dry note. Only the Harmony presets
    // carry dry, so this does nothing in the Whammy family.
    harmony_level_ = std::clamp(params.depth, 0.0f, 1.0f);

    // Speed is presented as Glide. Fast enough at the top to track a stomp,
    // slow enough at the bottom for a lazy sweep.
    const float speed = std::clamp((params.speed - 0.05f) / 9.95f, 0.0f, 1.0f);
    glide_ = 0.02f + speed * 0.95f;

    if (!seeded_) {
        seeded_ = true;
        semitones_ = semitone_target_;
        ratio_ = std::pow(2.0f, semitones_ / 12.0f);
    }

    // Glide in the pitch domain, then convert once per control block.
    semitones_ += glide_ * (semitone_target_ - semitones_);
    const float target_ratio = std::pow(2.0f, semitones_ / 12.0f);

    // Retune the anti-alias filter for the block, then ramp the ratio to it.
    // In Detune the R voice sits as far above the note as L sits below it.
    spread_ratio_ = detune_ > 0.0f ? std::pow(2.0f, 2.0f * detune_ / 12.0f) : 1.0f;
    voices_[0].shifter.SetShift(semitones_);
    // Tune each voice's anti-alias filter for the pitch it actually reads at.
    // The right voice runs at the left one times the spread, so its pitch is
    // the left pitch plus twice the detune. Mirroring the left pitch instead
    // was right only once a glide had settled: gliding from an upward preset
    // into Detune left the right filter off while that voice still read
    // upward, and it aliased (review of #88).
    voices_[1].shifter.SetShift(detune_ > 0.0f ? semitones_ + 2.0f * detune_ : semitones_);
    ratio_step_ = (target_ratio - ratio_) / static_cast<float>(BLOCK_SIZE);

    // The shifted voice carries the guitar's energy up or down with it, so the
    // tilt's pivot follows the ratio. The anti-alias filter and the guitar's
    // falling spectrum make the full ratio overshoot; ratio^0.7 fitted best,
    // keeping both ends of Tone within 2 dB from two octaves up to one down
    // on DI and synthetic guitar, against 5.1 dB with a fixed pivot.
    const float pivot = 400.0f * std::pow(target_ratio, 0.7f);
    for (auto& voice : voices_) {
        voice.tone.SetPivotHz(pivot);
        voice.tone.SetKnob(params.tone);
    }
}

StereoFrame WhammyMode::Process(StereoFrame input, const ParamSet& params) {
    (void)params;
    ratio_ += ratio_step_;
    voices_[0].shifter.SetRatioFast(ratio_);
    voices_[1].shifter.SetRatioFast(ratio_ * spread_ratio_);

    const auto shift = [](Voice& voice, float x) {
        return voice.dc.Process(voice.tone.Process(voice.shifter.Process(x)));
    };
    const float wet_l = shift(voices_[0], input.left);
    const float wet_r = shift(voices_[1], input.right);

    if (!harmony_) {
        // Whammy family carries no dry signal, as on the original.
        return {wet_l, wet_r};
    }

    // Harmony and Detune sit the shifted voice against the dry note. The
    // engine applies its own dry/wet mix on top; this is the internal balance.
    return {input.left + wet_l * harmony_level_, input.right + wet_r * harmony_level_};
}

} // namespace pedal
