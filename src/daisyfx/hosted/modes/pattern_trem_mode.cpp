#include "pattern_trem_mode.h"
#include "../config/constants.h"

using namespace pedal::mod_fx;

namespace pedal {

void PatternTremMode::Init() {
    tone_l_.Init();
    tone_r_.Init();
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
    tone_l_.SetKnob(params.tone);
    tone_r_.SetKnob(params.tone);
}

StereoFrame PatternTremMode::Process(StereoFrame input, const ParamSet& params) {
    gate_ = seq_.Process();

    // Smooth gate edges to avoid clicks (simple one-pole envelope).
    // attack ≈ 7 ms, release ≈ 3.5 ms 99%-settling at 48 kHz.
    // Keep short enough to preserve pattern definition at fast tempos.
    static constexpr float attack  = 0.01f;
    static constexpr float release = 0.005f;
    const float coef = (gate_ > smoothed_) ? attack : release;
    smoothed_ += coef * (gate_ - smoothed_);

    // Apply tremolo: mix between 0 and input based on depth
    const float trem_gain = 1.0f - params.depth * (1.0f - smoothed_);
    return {tone_l_.Process(input.left * trem_gain), tone_r_.Process(input.right * trem_gain)};
}

} // namespace pedal
