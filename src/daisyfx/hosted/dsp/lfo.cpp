#include "lfo.h"

namespace pedal {

static constexpr float TWO_PI    = 6.28318530717958647692f;
static constexpr float PI        = 3.14159265358979323846f;
// Per-sample soft-start coefficient: tau ≈ 50 ms. In PrepareBlock(), multiply by BLOCK_SIZE.
static constexpr float RAMP_COEFF = 1.0f / (0.05f * SAMPLE_RATE);

void Lfo::Init(float rate_hz, LfoWave wave) {
    phase_        = 0.0f;
    phase_offset_ = 0.0f;
    amplitude_    = 0.0f;
    sh_value_     = 0.0f;
    smooth_value_ = 0.0f;
    rand_         = 12345;
    jitter_       = 0.0f;
    wave_         = wave;
    SetRate(rate_hz);
    phase_inc_ = phase_inc_base_;
}

void Lfo::SetRate(float rate_hz) {
    const float old_base = phase_inc_base_;
    const float old_inc  = phase_inc_;
    phase_inc_base_ = TWO_PI * rate_hz * INV_SAMPLE_RATE;
    slew_coeff_     = 4.0f * rate_hz * INV_SAMPLE_RATE;
    if (slew_coeff_ > 1.0f) slew_coeff_ = 1.0f;
    if (old_base > 0.0f && old_inc > 0.0f) {
        phase_inc_ = phase_inc_base_ * (old_inc / old_base);
    } else {
        phase_inc_ = phase_inc_base_;
    }
}

static float lfo_compute(float phase, LfoWave wave) {
    switch (wave) {
        case LfoWave::Sine:
            return fast_sin(phase);
        case LfoWave::Triangle:
            return (phase < PI)
                ? (-1.0f + phase * (2.0f / PI))
                : ( 3.0f - phase * (2.0f / PI));
        case LfoWave::Square:
            return (phase < PI) ? 1.0f : -1.0f;
        case LfoWave::RampUp:
            return -1.0f + phase * (2.0f / TWO_PI);
        case LfoWave::RampDown:
            return 1.0f - phase * (2.0f / TWO_PI);
        case LfoWave::Exponential: {
            const float s = fast_sin(phase);
            return (s >= 0.0f) ? (s * s) : -(s * s);
        }
        default:
            return 0.0f;
    }
}

float Lfo::Process() {
    float out;
    if (wave_ == LfoWave::SampleAndHold) {
        out = sh_value_;
    } else if (wave_ == LfoWave::SmoothRandom) {
        smooth_value_ += slew_coeff_ * (sh_value_ - smooth_value_);
        out = smooth_value_;
    } else {
        out = lfo_compute(phase_, wave_);
    }
    amplitude_ += RAMP_COEFF * (1.0f - amplitude_);
    out *= amplitude_;

    phase_ += phase_inc_;
    while (phase_ >= TWO_PI) {
        phase_ -= TWO_PI;
        rand_     = lcg_next(rand_);
        sh_value_ = lcg_to_float(rand_);
        if (jitter_ > 0.0f) {
            rand_         = lcg_next(rand_);
            const float j = lcg_to_float(rand_) * jitter_ * 0.05f;
            phase_inc_    = phase_inc_base_ * (1.0f + j);
        } else {
            phase_inc_    = phase_inc_base_;
        }
    }
    while (phase_ < 0.0f) { phase_ += TWO_PI; }
    return out;
}

float Lfo::PrepareBlock() {
    float out;
    if (wave_ == LfoWave::SampleAndHold) {
        out = sh_value_;
    } else if (wave_ == LfoWave::SmoothRandom) {
        float block_slew = slew_coeff_ * static_cast<float>(BLOCK_SIZE);
        if (block_slew > 1.0f) block_slew = 1.0f;
        smooth_value_ += block_slew * (sh_value_ - smooth_value_);
        out = smooth_value_;
    } else {
        out = lfo_compute(phase_, wave_);
    }
    amplitude_ += (RAMP_COEFF * static_cast<float>(BLOCK_SIZE)) * (1.0f - amplitude_);
    out *= amplitude_;

    phase_ += phase_inc_ * static_cast<float>(BLOCK_SIZE);
    while (phase_ >= TWO_PI) {
        phase_ -= TWO_PI;
        rand_     = lcg_next(rand_);
        sh_value_ = lcg_to_float(rand_);
        if (jitter_ > 0.0f) {
            rand_         = lcg_next(rand_);
            const float j = lcg_to_float(rand_) * jitter_ * 0.05f;
            phase_inc_    = phase_inc_base_ * (1.0f + j);
        } else {
            phase_inc_    = phase_inc_base_;
        }
    }
    while (phase_ < 0.0f) { phase_ += TWO_PI; }
    return out;
}

} // namespace pedal
