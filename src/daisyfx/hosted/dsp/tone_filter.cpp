#include "tone_filter.h"

namespace pedal {

static constexpr float kTwoPi        = 6.28318530717958647692f;
static constexpr float kButterworthQ = 0.707106781f;  // 1/sqrt(2): maximally flat

void ToneFilter::ComputeLpCoeffs(float fc, float q,
                                  float& b0, float& b1, float& b2,
                                  float& a1, float& a2) const {
    const float w0    = kTwoPi * fc * inv_sample_rate_;
    const float alpha = sinf(w0) / (2.0f * q);
    const float cw    = cosf(w0);
    const float inv_a0 = 1.0f / (1.0f + alpha);
    b0 = (1.0f - cw) * 0.5f * inv_a0;
    b1 = (1.0f - cw) * inv_a0;
    b2 = b0;
    a1 = -2.0f * cw * inv_a0;
    a2 = (1.0f - alpha) * inv_a0;
}

void ToneFilter::ComputeHpCoeffs(float fc, float q,
                                  float& b0, float& b1, float& b2,
                                  float& a1, float& a2) const {
    const float w0    = kTwoPi * fc * inv_sample_rate_;
    const float alpha = sinf(w0) / (2.0f * q);
    const float cw    = cosf(w0);
    const float inv_a0 = 1.0f / (1.0f + alpha);
    b0 = (1.0f + cw) * 0.5f * inv_a0;
    b1 = -(1.0f + cw) * inv_a0;
    b2 = b0;
    a1 = -2.0f * cw * inv_a0;
    a2 = (1.0f - alpha) * inv_a0;
}

void ToneFilter::Init(float sample_rate) {
    inv_sample_rate_ = sample_rate > 0.0f ? (1.0f / sample_rate) : INV_SAMPLE_RATE;
    lp_s1_ = lp_s2_ = 0.0f;
    hp_s1_ = hp_s2_ = 0.0f;
    last_knob_ = -1.0f;
    SetKnob(0.5f);
}

void ToneFilter::SetKnob(float knob) {
    // Float equality is intentional: knob comes from a quantized pot/param
    // value, not from arithmetic, so bit-identical repeats are expected.
    if (knob == last_knob_) return;
    last_knob_ = knob;

    float lp_fc, hp_fc;
    if (knob <= 0.5f) {
        const float t = knob * 2.0f;
        lp_fc    = 200.0f + t * 7800.0f;  // 200 → 8000 Hz
        hp_fc    = 20.0f;
        lp_mix_  = 1.0f;
        hp_mix_  = 0.0f;
    } else {
        const float t = (knob - 0.5f) * 2.0f;
        lp_fc    = 8000.0f + t * 12000.0f;         // 8000 → 20000 Hz
        hp_fc    = 20.0f + t * t * 2980.0f;        // 20 → 3000 Hz
        lp_mix_  = 1.0f - t;
        hp_mix_  = t;
    }

    ComputeLpCoeffs(lp_fc, kButterworthQ, lp_b0_, lp_b1_, lp_b2_, lp_a1_, lp_a2_);
    ComputeHpCoeffs(hp_fc, kButterworthQ, hp_b0_, hp_b1_, hp_b2_, hp_a1_, hp_a2_);
}

float ToneFilter::Process(float sample) {
    // LP biquad — Direct Form II transposed
    const float lp_y = lp_b0_ * sample + lp_s1_;
    lp_s1_ = lp_b1_ * sample - lp_a1_ * lp_y + lp_s2_;
    lp_s2_ = lp_b2_ * sample - lp_a2_ * lp_y;

    // HP biquad applied to LP output (series connection)
    const float hp_y = hp_b0_ * lp_y + hp_s1_;
    hp_s1_ = hp_b1_ * lp_y - hp_a1_ * hp_y + hp_s2_;
    hp_s2_ = hp_b2_ * lp_y - hp_a2_ * hp_y;

    return lp_mix_ * lp_y + hp_mix_ * hp_y;
}

} // namespace pedal
