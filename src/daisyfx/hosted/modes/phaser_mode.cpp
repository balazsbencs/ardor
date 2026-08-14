#include "phaser_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include "../dsp/freq_table.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

// Stage counts per sub-mode. Barber Pole (6) uses two 4-stage chains — value unused in that path.
static const int kStageCounts[] = {2, 4, 6, 8, 12, 16, 4};

void PhaserMode::Init() {
    Reset();
}

void PhaserMode::Reset() {
    static constexpr float kHalfPi = 1.57079633f;
    lfo_.Init(0.5f, LfoWave::Sine);
    lfo_.SetJitter(0.05f);
    lfo2_.Init(0.5f, LfoWave::Sine);
    lfo2_.SetJitter(0.05f);
    lfo2_.SetPhaseOffset(kHalfPi);   // 90° quadrature offset for Barber Pole
    lfo2_.Reset();                   // apply offset to phase_ (SetPhaseOffset alone does not)
    for (auto& s : stages_) s.Reset();
    for (auto& s : stages_r_) s.Reset();
    feedback_r_ = 0.0f;
    dc_.Init();
    dc2_.Init();
    sweep_low_  = 0.0f;
    sweep_span_ = 0.4f;
    barber_phase_ = 0.0f;
    barber_inc_ = 0.0f;
    feedback_  = 0.0f;
    feedback2_ = 0.0f;
    num_stages_  = 4;
    barber_pole_ = false;
}

void PhaserMode::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.speed);
    lfo2_.SetRate(params.speed);

    // Sub-mode from p2: 0..6 → stage counts (6 = Barber Pole)
    int sub = static_cast<int>(params.p2 * 6.999f);
    if (sub < 0) sub = 0;
    if (sub > 6) sub = 6;
    barber_pole_ = (sub == 6);
    num_stages_  = kStageCounts[sub];

    // Sweep range in log frequency, not in allpass coefficient. Tone picks the
    // centre (300 Hz dark .. 10 kHz bright) and Depth the width in octaves.
    // Sweeping the coefficient linearly, as this once did, races through the
    // top of the range and crawls through the bottom.
    const float centre_hz = 300.0f * std::exp2(params.tone * 5.06f);  // 300 Hz .. ~10 kHz
    const float centre    = freq_table::position_for_hz(centre_hz);
    const float span      = params.depth * 0.32f;   // up to ~3.2 octaves
    sweep_low_  = centre - span * 0.5f;
    sweep_span_ = span;

    // Barber pole ramps its own phase; the LFO rate sets how fast notches rise.
    barber_inc_ = params.speed * INV_SAMPLE_RATE;
    // LFO coefficients computed per-sample in Process() to avoid block-boundary zipper noise.
}

float PhaserMode::sweepCoeff(float phase) const
{
    return freq_table::allpass_coeff_at(sweep_low_ + sweep_span_ * phase);
}

StereoFrame PhaserMode::Process(StereoFrame input, const ParamSet& params) {
    const float regen = params.p1 * 0.95f;

    if (barber_pole_) {
        // A barber pole needs notches that rise without end. That requires a
        // monotonic ramp, not a sine: driving the crossfade from a sine made it
        // reverse direction every half cycle, which is an ordinary phaser with a
        // wobble. Two chains half a cycle apart are crossfaded by a sawtooth, so
        // as one chain runs off the top the other has already restarted at the
        // bottom and takes over while it is silent.
        barber_phase_ += barber_inc_;
        if (barber_phase_ >= 1.0f) barber_phase_ -= 1.0f;
        const float phase_a = barber_phase_;
        const float phase_b = phase_a >= 0.5f ? phase_a - 0.5f : phase_a + 0.5f;

        // Sweep each chain from the bottom of its range to the top, then jump
        // back while its crossfade gain is zero.
        const float coeff_a = sweepCoeff(phase_a);
        const float coeff_b = sweepCoeff(phase_b);

        static constexpr float kFbDrive2 = 2.0f;
        const float fb_a_clipped = soft_clip_tanh(feedback_  * kFbDrive2) / kFbDrive2;
        const float fb_b_clipped = soft_clip_tanh(feedback2_ * kFbDrive2) / kFbDrive2;
        float xa = input.mono() + fb_a_clipped * regen;
        float xb = input.mono() + fb_b_clipped * regen;
        for (int i = 0; i < 4; ++i) {
            stages_[i].SetCoeff(coeff_a);
            xa = stages_[i].Process(xa);
        }
        for (int i = 4; i < 8; ++i) {
            stages_[i].SetCoeff(coeff_b);
            xb = stages_[i].Process(xb);
        }
        xa = dc_.Process(xa);
        xb = dc2_.Process(xb);
        feedback_  = xa;
        feedback2_ = xb;

        // Raised-cosine crossfade: zero at the wrap point of each chain, so the
        // coefficient jump there is inaudible.
        const float gain_a = 0.5f - 0.5f * fast_cos_full(phase_a * 6.28318530718f);
        const float gain_b = 0.5f - 0.5f * fast_cos_full(phase_b * 6.28318530718f);

        // Keep the stereo the rest of the phaser produces: the two chains are
        // already half a cycle apart, so weighting them oppositely gives width.
        const float left  = xa * gain_a + xb * gain_b;
        const float right = xa * gain_b + xb * gain_a;
        return {left, right};
    }

    // Normal path: always stereo. L uses stages_[], R uses stages_r_[].
    // Both chains run lfo_ and lfo2_ (90° quadrature) for all stage counts.
    // Positions are normalised log-frequency, so the sweep is even to the ear.
    const float lfo_val  = lfo_.Process();
    const float lfo_val2 = lfo2_.Process();
    const float pos_l = sweep_low_ + sweep_span_ * (0.5f + 0.5f * lfo_val);
    const float pos_r = sweep_low_ + sweep_span_ * (0.5f + 0.5f * lfo_val2);

    // Stagger alternate stages by a fixed fraction of an octave so the notches
    // spread instead of stacking. In log-frequency this is a constant offset.
    constexpr float kStagger = 0.012f;

    // Soft-clip feedback before injection: limits self-oscillation organically.
    // drive = 2.0 gives unity gain for small signals, soft limit around ±0.5.
    // Uses the shared Padé curve rather than libm tanhf(), which was three
    // calls per sample in the audio path.
    static constexpr float kFbDrive = 2.0f;
    const float fb_l_clipped = soft_clip_tanh(feedback_ * kFbDrive) / kFbDrive;
    float xl = input.mono() + fb_l_clipped * regen;
    for (int i = 0; i < num_stages_; ++i) {
        const float offset = (i & 1) ? kStagger : -kStagger;
        stages_[i].SetCoeff(freq_table::allpass_coeff_at(pos_l + offset));
        xl = stages_[i].Process(xl);
    }
    xl = dc_.Process(xl);
    feedback_ = xl;

    const float fb_r_clipped = soft_clip_tanh(feedback_r_ * kFbDrive) / kFbDrive;
    float xr = input.mono() + fb_r_clipped * regen;
    for (int i = 0; i < num_stages_; ++i) {
        const float offset = (i & 1) ? kStagger : -kStagger;
        stages_r_[i].SetCoeff(freq_table::allpass_coeff_at(pos_r + offset));
        xr = stages_r_[i].Process(xr);
    }
    xr = dc2_.Process(xr);
    feedback_r_ = xr;

    return {xl, xr};
}

} // namespace pedal
