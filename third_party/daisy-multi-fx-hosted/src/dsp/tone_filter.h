#pragma once
#include "../config/constants.h"
#include <cmath>

namespace pedal {

// 2nd-order Direct Form II transposed biquad tone filter.
// knob: 0=dark LP, 0.5=flat, 1=bright HP blend
class ToneFilter {
public:
    void Init(float sample_rate = SAMPLE_RATE);
    void SetKnob(float knob);
    float Process(float sample);

private:
    void ComputeLpCoeffs(float fc, float q,
                         float& b0, float& b1, float& b2,
                         float& a1, float& a2) const;
    void ComputeHpCoeffs(float fc, float q,
                         float& b0, float& b1, float& b2,
                         float& a1, float& a2) const;

    float last_knob_ = -1.0f;
    float inv_sample_rate_ = INV_SAMPLE_RATE;

    // LP biquad (DF2T state)
    float lp_b0_ = 1.0f, lp_b1_ = 0.0f, lp_b2_ = 0.0f;
    float lp_a1_ = 0.0f, lp_a2_ = 0.0f;
    float lp_s1_ = 0.0f, lp_s2_ = 0.0f;

    // HP biquad applied to LP output (DF2T state)
    float hp_b0_ = 1.0f, hp_b1_ = 0.0f, hp_b2_ = 0.0f;
    float hp_a1_ = 0.0f, hp_a2_ = 0.0f;
    float hp_s1_ = 0.0f, hp_s2_ = 0.0f;

    float lp_mix_ = 1.0f;
    float hp_mix_ = 0.0f;
};

} // namespace pedal
