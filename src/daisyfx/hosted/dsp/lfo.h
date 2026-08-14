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
    void Init(float rate_hz = 1.0f, LfoWave wave = LfoWave::Sine,
              float sample_rate = SAMPLE_RATE);
    void Reset() { phase_ = phase_offset_; amplitude_ = 0.0f; smooth_value_ = 0.0f; sh_value_ = 0.0f; rand_ = 12345; phase_inc_ = phase_inc_base_; }
    void SetRate(float rate_hz);
    void SetWave(LfoWave wave) { wave_ = wave; }
    void SetJitter(float amount) { jitter_ = amount; }

    // Copies the phase and the jittered increment from another oscillator, then
    // applies this one's own fixed offset.
    //
    // Several modes create two or three oscillators, set a fixed offset between
    // them (120 degrees for Chorus, 90 for Rotary's microphone pair and for
    // Flanger's stereo spread), then call SetJitter() on each one separately.
    // Each then perturbs its own increment from its own PRNG, so the offsets
    // random-walk apart and never come back — Rotary's "two microphones 90
    // degrees apart" stop being 90 degrees apart within seconds. Following a
    // leader keeps the relationship exact while still letting the pair drift
    // together.
    void FollowPhaseOf(const Lfo& leader) {
        static constexpr float TWO_PI = 6.28318530717958647692f;
        phase_ = leader.phase_ + phase_offset_;
        while (phase_ >= TWO_PI) phase_ -= TWO_PI;
        while (phase_ < 0.0f)    phase_ += TWO_PI;
        phase_inc_ = leader.phase_inc_;
    }
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
    float    ramp_coeff_     = 0.0f;
    float    sample_rate_    = SAMPLE_RATE;
    float    jitter_         = 0.0f;
    LfoWave  wave_           = LfoWave::Sine;
    uint32_t rand_           = 12345;
};

} // namespace pedal
