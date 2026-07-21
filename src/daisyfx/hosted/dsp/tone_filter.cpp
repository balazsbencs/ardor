#include "tone_filter.h"

#include <algorithm>

namespace pedal {

static constexpr float kTwoPi        = 6.28318530717958647692f;
static constexpr float kButterworthQ = 0.707106781f;  // 1/sqrt(2): maximally flat
static constexpr float kMinimumCutoff = 20.0f;
static constexpr float kDarkCutoff = 200.0f;
static constexpr float kBrightCutoff = 3000.0f;

static float log_lerp(float from, float to, float amount) {
    return expf(logf(from) + amount * (logf(to) - logf(from)));
}

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
    sample_rate_ = std::isfinite(sample_rate) && sample_rate > 0.0f ? sample_rate : SAMPLE_RATE;
    inv_sample_rate_ = 1.0f / sample_rate_;
    shape_ = Shape::Bypass;
    mix_ = 0.0f;
    last_knob_ = -1.0f;
    Reset();
    SetKnob(0.5f);
}

void ToneFilter::Reset() {
    lp_s1_ = lp_s2_ = 0.0f;
    hp_s1_ = hp_s2_ = 0.0f;
}

void ToneFilter::SetKnob(float knob) {
    if (!std::isfinite(knob)) knob = 0.5f;
    knob = std::clamp(knob, 0.0f, 1.0f);

    // Float equality is intentional: live parameter values are held constant
    // between control-rate updates, so bit-identical repeats are expected.
    if (knob == last_knob_) return;
    last_knob_ = knob;

    const Shape next_shape = knob < 0.5f ? Shape::LowPass
                           : knob > 0.5f ? Shape::HighPass
                                         : Shape::Bypass;
    if (next_shape != shape_) {
        // The inactive filter has not been updated. Clearing its state prevents
        // an old resonance from leaking in when the knob crosses centre.
        Reset();
        shape_ = next_shape;
    }

    if (shape_ == Shape::Bypass) {
        mix_ = 0.0f;
        return;
    }

    // RBJ biquad coefficients are only stable below Nyquist. Keeping the
    // maximum at 45% of the active rate provides a useful transition band and
    // makes the 24 kHz reverb stage safe at a fully bright setting.
    const float max_cutoff = std::max(kDarkCutoff, std::min(20000.0f, sample_rate_ * 0.45f));
    if (shape_ == Shape::LowPass) {
        mix_ = 1.0f - knob * 2.0f;
        const float cutoff = log_lerp(max_cutoff, kDarkCutoff, mix_);
        ComputeLpCoeffs(cutoff, kButterworthQ, lp_b0_, lp_b1_, lp_b2_, lp_a1_, lp_a2_);
    } else {
        mix_ = (knob - 0.5f) * 2.0f;
        const float maximum_bright_cutoff = std::max(kMinimumCutoff, std::min(kBrightCutoff, max_cutoff));
        const float cutoff = log_lerp(kMinimumCutoff, maximum_bright_cutoff, mix_);
        ComputeHpCoeffs(cutoff, kButterworthQ, hp_b0_, hp_b1_, hp_b2_, hp_a1_, hp_a2_);
    }
}

float ToneFilter::Process(float sample) {
    if (!std::isfinite(sample)) {
        Reset();
        return 0.0f;
    }
    if (shape_ == Shape::Bypass) return sample;

    float filtered;
    if (shape_ == Shape::LowPass) {
        filtered = lp_b0_ * sample + lp_s1_;
        lp_s1_ = lp_b1_ * sample - lp_a1_ * filtered + lp_s2_;
        lp_s2_ = lp_b2_ * sample - lp_a2_ * filtered;
    } else {
        filtered = hp_b0_ * sample + hp_s1_;
        hp_s1_ = hp_b1_ * sample - hp_a1_ * filtered + hp_s2_;
        hp_s2_ = hp_b2_ * sample - hp_a2_ * filtered;
    }

    const float output = sample + mix_ * (filtered - sample);
    if (!std::isfinite(output) || !std::isfinite(lp_s1_) || !std::isfinite(lp_s2_)
        || !std::isfinite(hp_s1_) || !std::isfinite(hp_s2_)) {
        Reset();
        return 0.0f;
    }
    return output;
}

} // namespace pedal
