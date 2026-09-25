#include "pattern_trem_mode.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

void PatternTremMode::Init() {
    tone_l_.Init(SAMPLE_RATE, ToneGain::Loudness);
    tone_r_.Init(SAMPLE_RATE, ToneGain::Loudness);
    Reset();
}

void PatternTremMode::Reset() {
    seq_.Reset();
    gate_     = 0.0f;
    smoothed_ = 1.0f;
    tone_l_.Reset();
    tone_r_.Reset();
}

void PatternTremMode::Prepare(const ParamSet& params) {
    // Speed (0.05–10 Hz) → beat period in samples
    const float beat_period = SAMPLE_RATE / params.speed;

    // P1: pattern select (0–15)
    const int pattern = static_cast<int>(params.p1 * 15.999f);

    // P2: subdivision (0=straight 16ths, 0.33=8ths, 0.66=triplets)
    int steps_per_beat;
    const int subdiv_idx = static_cast<int>(params.p2 * 2.999f);
    switch (subdiv_idx) {
        case 0:  steps_per_beat = 4; break;   // 16th notes
        case 1:  steps_per_beat = 2; break;   // 8th notes
        default: steps_per_beat = 3; break;   // triplets
    }

    seq_.SetPeriodSamples(beat_period);
    seq_.SetPattern(pattern, steps_per_beat);
    // Swing (p4): 50 % (straight) to 75 % (hard shuffle).
    seq_.SetSwing(0.5f + params.p4 * 0.25f);

    // Smooth (p3): edge time constant 0.5 ms .. 30 ms, logarithmic. The
    // release is twice the attack, as the fixed edges always were; 0.35 lands
    // on the ~2 ms attack this mode used before the control.
    const float attack_seconds = 0.0005f * std::pow(60.0f, params.p3);
    attack_  = 1.0f - std::exp(-1.0f / (attack_seconds * SAMPLE_RATE));
    release_ = 1.0f - std::exp(-1.0f / (2.0f * attack_seconds * SAMPLE_RATE));
    tone_l_.SetKnob(params.tone);
    tone_r_.SetKnob(params.tone);
}

StereoFrame PatternTremMode::Process(StereoFrame input, const ParamSet& params) {
    gate_ = seq_.Process();

    // Smooth gate edges to avoid clicks (one-pole envelope, set by Smooth).
    const float coef = (gate_ > smoothed_) ? attack_ : release_;
    smoothed_ += coef * (gate_ - smoothed_);

    // Apply tremolo: mix between 0 and input based on depth
    // No make-up gain here. Unlike a continuous tremolo, a gated pattern
    // already sits at unity while the gate is open, so there is no level drop
    // to correct and boosting would only flatten the pattern's dynamics.
    const float trem_gain = 1.0f - params.depth * (1.0f - smoothed_);
    return {tone_l_.Process(input.left * trem_gain), tone_r_.Process(input.right * trem_gain)};
}

} // namespace pedal
