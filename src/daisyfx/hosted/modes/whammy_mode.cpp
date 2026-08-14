#include "whammy_mode.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

namespace {

struct IntervalPair {
    float heel;   // pedal at 0
    float toe;    // pedal at 1
};

// Whammy family: heel is always unison, toe is the named interval.
constexpr IntervalPair kWhammy[WhammyMode::WHAMMY_PRESETS] = {
    {0.0f,  24.0f},   // 2 OCT UP
    {0.0f,  12.0f},   // 1 OCT UP
    {0.0f,   7.0f},   // 5TH UP
    {0.0f,   5.0f},   // 4TH UP
    {0.0f,  -2.0f},   // 2ND DN
    {0.0f,  -5.0f},   // 4TH DN
    {0.0f,  -7.0f},   // 5TH DN
    {0.0f, -12.0f},   // 1 OCT DN
    {0.0f, -24.0f},   // 2 OCT DN
    {0.0f, -36.0f},   // DIVE BOMB
};

// Harmony family: the pedal morphs between two fixed chromatic intervals.
// Heel is the "toe up" reading on the original unit, toe the "toe down" one.
constexpr IntervalPair kHarmony[WhammyMode::HARMONY_PRESETS] = {
    {-12.0f,  12.0f},   // OCT DN / OCT UP
    { -7.0f,  -5.0f},   // 5TH DN / 4TH DN
    { -5.0f,  -3.0f},   // 4TH DN / 3RD DN
    {  7.0f,  10.0f},   // 5TH UP / 7TH UP
    {  7.0f,   9.0f},   // 5TH UP / 6TH UP
    {  5.0f,   7.0f},   // 4TH UP / 5TH UP
    {  4.0f,   5.0f},   // 3RD UP / 4TH UP
    {  3.0f,   4.0f},   // MIN 3RD UP / 3RD UP
    {  2.0f,   4.0f},   // 2ND UP / 3RD UP
};

} // namespace

void WhammyMode::Init() {
    shifter_.Init(buf_, kBufSize, SAMPLE_RATE, kGrainSize);
    tone_.Init();
    Reset();
}

void WhammyMode::Reset() {
    shifter_.Reset();
    tone_.Reset();
    dc_.Init();
    semitones_       = 0.0f;
    semitone_target_ = 0.0f;
    ratio_           = 1.0f;
    ratio_step_      = 0.0f;
    harmony_         = false;
    preset_          = 0;
    seeded_          = false;
}

void WhammyMode::Prepare(const ParamSet& params) {
    // Mode knob: upper half is the Whammy family, lower half Harmony. The
    // shared mod default for depth is 0.70, so an untouched preset lands on
    // Whammy.
    harmony_ = params.depth < 0.5f;

    const int count = harmony_ ? HARMONY_PRESETS : WHAMMY_PRESETS;
    const int candidate = std::clamp(
        static_cast<int>(params.p2 * static_cast<float>(count) * 0.999f), 0, count - 1);
    preset_ = candidate;

    const IntervalPair pair = harmony_ ? kHarmony[preset_] : kWhammy[preset_];
    const float pedal = std::clamp(params.p1, 0.0f, 1.0f);
    semitone_target_ = pair.heel + (pair.toe - pair.heel) * pedal;

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
    shifter_.SetShift(semitones_);
    ratio_step_ = (target_ratio - ratio_) / static_cast<float>(BLOCK_SIZE);

    tone_.SetKnob(params.tone);
}

StereoFrame WhammyMode::Process(StereoFrame input, const ParamSet& params) {
    ratio_ += ratio_step_;
    shifter_.SetRatioFast(ratio_);

    const float mono = input.mono();
    const float wet = dc_.Process(tone_.Process(shifter_.Process(mono)));

    if (!harmony_) {
        // Whammy family carries no dry signal, as on the original.
        return {wet, wet};
    }

    // Harmony family sits the shifted voice against the dry note. The engine
    // applies its own dry/wet mix on top; this is the internal balance.
    const float out = mono + wet;
    return {out, out};
}

} // namespace pedal
