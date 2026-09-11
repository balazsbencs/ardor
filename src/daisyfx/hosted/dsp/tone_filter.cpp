#include "tone_filter.h"

#include <algorithm>
#include <cmath>

namespace pedal {

static constexpr float kTwoPi = 6.28318530717958647692f;

float ToneFilter::Biquad::Process(float input) {
    const float output = b0 * input + s1;
    s1 = b1 * input - a1 * output + s2;
    s2 = b2 * input - a2 * output;
    return output;
}

// RBJ low/high shelf, S=1. The overlapping shelves produce a broad tilt
// without the hollow response caused by mixing steep LP/HP filters with dry.
void ToneFilter::ComputeShelf(bool high, float fc, float gain_db, Biquad& f) const {
    const float A = std::pow(10.0f, gain_db / 40.0f);
    const float w0 = kTwoPi * fc * inv_sample_rate_;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = 0.5f * sw * std::sqrt(2.0f);
    const float beta = 2.0f * std::sqrt(A) * alpha;
    float a0;
    if (high) {
        f.b0 = A * ((A + 1.0f) + (A - 1.0f) * cw + beta);
        f.b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
        f.b2 = A * ((A + 1.0f) + (A - 1.0f) * cw - beta);
        a0   = (A + 1.0f) - (A - 1.0f) * cw + beta;
        f.a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
        f.a2 = (A + 1.0f) - (A - 1.0f) * cw - beta;
    } else {
        f.b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + beta);
        f.b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
        f.b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - beta);
        a0   = (A + 1.0f) + (A - 1.0f) * cw + beta;
        f.a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
        f.a2 = (A + 1.0f) + (A - 1.0f) * cw - beta;
    }
    const float inverse = 1.0f / a0;
    f.b0 *= inverse; f.b1 *= inverse; f.b2 *= inverse;
    f.a1 *= inverse; f.a2 *= inverse;
}

void ToneFilter::Init(float sample_rate) {
    sample_rate_ = std::isfinite(sample_rate) && sample_rate > 0.0f ? sample_rate : SAMPLE_RATE;
    inv_sample_rate_ = 1.0f / sample_rate_;
    last_knob_ = -1.0f;
    Reset();
    SetKnob(0.5f);
}

void ToneFilter::Reset() {
    low_shelf_.Reset();
    high_shelf_.Reset();
}

void ToneFilter::SetKnob(float knob) {
    if (!std::isfinite(knob)) knob = 0.5f;
    knob = std::clamp(knob, 0.0f, 1.0f);
    if (knob == last_knob_) return;
    last_knob_ = knob;

    const float amount = (knob - 0.5f) * 2.0f;
    const bool next_bypass = std::fabs(amount) < 0.0001f;
    if (next_bypass != bypass_) Reset();
    bypass_ = next_bypass;
    if (bypass_) {
        output_gain_ = 1.0f;
        return;
    }

    ComputeShelf(false, 500.0f, -4.0f * amount, low_shelf_);
    ComputeShelf(true, 2000.0f, 7.0f * amount, high_shelf_);
    output_gain_ = std::pow(10.0f, -std::fabs(amount) * 3.0f / 20.0f);
}

float ToneFilter::Process(float sample) {
    if (!std::isfinite(sample)) {
        Reset();
        return 0.0f;
    }
    if (bypass_) return sample;
    const float output = high_shelf_.Process(low_shelf_.Process(sample)) * output_gain_;
    if (!std::isfinite(output) || !std::isfinite(low_shelf_.s1) || !std::isfinite(low_shelf_.s2)
        || !std::isfinite(high_shelf_.s1) || !std::isfinite(high_shelf_.s2)) {
        Reset();
        return 0.0f;
    }
    return output;
}

} // namespace pedal
