#include "phaser_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
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
    center_    = -0.5f;
    depth_mod_ = 0.0f;
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

    // Cache center frequency and depth swing for per-sample LFO use in Process().
    // center: tone=0 → -0.95 (notch ~300 Hz), tone=1 → -0.10 (notch ~10 kHz)
    center_    = -(0.95f - params.tone * 0.85f);
    depth_mod_ = params.depth * 0.4f;
    // LFO coefficients computed per-sample in Process() to avoid block-boundary zipper noise.
}

StereoFrame PhaserMode::Process(StereoFrame input, const ParamSet& params) {
    const float regen = params.p1 * 0.95f;

    if (barber_pole_) {
        const float lfo_val = lfo_.Process();
        float coeff = center_ + depth_mod_ * lfo_val;
        if (coeff > -0.01f) coeff = -0.01f;
        if (coeff < -0.99f) coeff = -0.99f;

        const float lfo_val2 = lfo2_.Process();
        float coeff2 = center_ + depth_mod_ * lfo_val2;
        if (coeff2 > -0.01f) coeff2 = -0.01f;
        if (coeff2 < -0.99f) coeff2 = -0.99f;

        static constexpr float kFbDrive2 = 2.0f;
        const float fb_a_clipped = tanhf(feedback_  * kFbDrive2) / kFbDrive2;
        const float fb_b_clipped = tanhf(feedback2_ * kFbDrive2) / kFbDrive2;
        float xa = input.mono() + fb_a_clipped * regen;
        float xb = input.mono() + fb_b_clipped * regen;
        for (int i = 0; i < 4; ++i) {
            stages_[i].SetCoeff(coeff);
            xa = stages_[i].Process(xa);
        }
        for (int i = 4; i < 8; ++i) {
            stages_[i].SetCoeff(coeff2);
            xb = stages_[i].Process(xb);
        }
        xa = dc_.Process(xa);
        xb = dc2_.Process(xb);
        feedback_  = xa;
        feedback2_ = xb;

        const float t = (lfo_val + 1.0f) * 0.5f;
        const float gain_a = fast_sin(t * 1.57079633f);
        const float gain_b = fast_sin((1.0f - t) * 1.57079633f);

        const float out = xa * gain_a + xb * gain_b;
        return {out, out};
    }

    // Normal path: always stereo. L uses stages_[], R uses stages_r_[].
    // Both chains run lfo_ and lfo2_ (90° quadrature) for all stage counts.
    const float lfo_val = lfo_.Process();
    float coeff_l = center_ + depth_mod_ * lfo_val;
    if (coeff_l > -0.01f) coeff_l = -0.01f;
    if (coeff_l < -0.99f) coeff_l = -0.99f;

    // Soft-clip feedback before injection: limits self-oscillation organically.
    // drive = 2.0 gives unity gain for small signals, soft limit around ±0.5.
    static constexpr float kFbDrive = 2.0f;
    const float fb_l_clipped = tanhf(feedback_ * kFbDrive) / kFbDrive;
    float xl = input.mono() + fb_l_clipped * regen;
    for (int i = 0; i < num_stages_; ++i) {
        float sc = coeff_l;
        if (i & 1) sc += 0.04f * depth_mod_;
        else        sc -= 0.04f * depth_mod_;
        if (sc > -0.01f) sc = -0.01f;
        if (sc < -0.99f) sc = -0.99f;
        stages_[i].SetCoeff(sc);
        xl = stages_[i].Process(xl);
    }
    xl = dc_.Process(xl);
    feedback_ = xl;

    // R chain — quadrature LFO gives stereo width on all stage counts.
    const float lfo_val2 = lfo2_.Process();
    float coeff_r = center_ + depth_mod_ * lfo_val2;
    if (coeff_r > -0.01f) coeff_r = -0.01f;
    if (coeff_r < -0.99f) coeff_r = -0.99f;

    const float fb_r_clipped = tanhf(feedback_r_ * kFbDrive) / kFbDrive;
    float xr = input.mono() + fb_r_clipped * regen;
    for (int i = 0; i < num_stages_; ++i) {
        float sc = coeff_r;
        if (i & 1) sc += 0.04f * depth_mod_;
        else        sc -= 0.04f * depth_mod_;
        if (sc > -0.01f) sc = -0.01f;
        if (sc < -0.99f) sc = -0.99f;
        stages_r_[i].SetCoeff(sc);
        xr = stages_r_[i].Process(xr);
    }
    xr = dc2_.Process(xr);
    feedback_r_ = xr;

    return {xl, xr};
}

} // namespace pedal
