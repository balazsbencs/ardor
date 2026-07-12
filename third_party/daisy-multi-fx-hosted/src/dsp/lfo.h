#pragma once
#include "../config/constants.h"
#include "fast_math.h"

namespace pedal {

enum class LfoWave {
    Sine,
    Triangle,
    Square,
    RampUp,
    RampDown,
    SampleAndHold,
    Exponential,
    SmoothRandom,
};

class Lfo {
public:
    void Init(float rate_hz = 1.0f, LfoWave wave = LfoWave::Sine);
    void Reset() { phase_ = phase_offset_; amplitude_ = 0.0f; smooth_value_ = 0.0f; sh_value_ = 0.0f; rand_ = 12345; phase_inc_ = phase_inc_base_; }
    void SetRate(float rate_hz);
    void SetWave(LfoWave wave) { wave_ = wave; }
    void SetJitter(float amount) { jitter_ = amount; }
    void SetPhaseOffset(float offset_radians) {
        static constexpr float TWO_PI = 6.28318530717958647692f;
        if (offset_radians != offset_radians || offset_radians > 1e6f || offset_radians < -1e6f) {
            phase_offset_ = 0.0f;
            return;
        }
        while (offset_radians >= TWO_PI) offset_radians -= TWO_PI;
        while (offset_radians < 0.0f)    offset_radians += TWO_PI;
        phase_offset_ = offset_radians;
    }
    float GetPhase() const { return phase_; }
    float Process();
    float PrepareBlock();

private:
    float    phase_          = 0.0f;
    float    phase_inc_      = 0.0f;
    float    phase_inc_base_ = 0.0f;
    float    phase_offset_   = 0.0f;
    float    amplitude_      = 0.0f;
    float    sh_value_       = 0.0f;
    float    smooth_value_   = 0.0f;
    float    slew_coeff_     = 0.0f;
    float    jitter_         = 0.0f;
    LfoWave  wave_           = LfoWave::Sine;
    uint32_t rand_           = 12345;
};

} // namespace pedal
