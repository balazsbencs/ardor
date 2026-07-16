#include "swell_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

void SwellDelay::Init() {
    swell_line_.Init(swell_buf_, MAX_DELAY_SAMPLES);
    follower_.Init(5.0f, 80.0f);
    dc_.Init();
    state_                = SwellState::Idle;
    env_gain_             = 0.0f;
    prev_above_threshold_ = false;
}

void SwellDelay::Reset() {
    swell_line_.Reset();
    dc_.Init();
    state_                = SwellState::Idle;
    env_gain_             = 0.0f;
    attack_rate_          = 0.0f;
    decay_rate_           = 0.0f;
    delay_smooth_         = 0.0f;
    prev_above_threshold_ = false;
    fb_lim_.Reset();
}

void SwellDelay::Prepare(const ParamSet& params) {
    // Map modulation controls to musically useful AD envelope times.
    // mod_spd (0.05..10 Hz) -> attack time ~1.5s .. 0.02s
    float mod_spd_norm = (params.mod_spd - 0.05f) / (10.0f - 0.05f);
    if (mod_spd_norm < 0.0f) mod_spd_norm = 0.0f;
    if (mod_spd_norm > 1.0f) mod_spd_norm = 1.0f;
    const float attack_time_s = 1.5f - 1.48f * mod_spd_norm;

    // mod_dep (0..1) -> decay time ~2.5s .. 0.08s
    const float decay_time_s  = 2.5f - 2.42f * params.mod_dep;

    attack_rate_ = 1.0f / (attack_time_s * SAMPLE_RATE);
    decay_rate_  = 1.0f / (decay_time_s * SAMPLE_RATE);
}

StereoFrame SwellDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.001f;

    delay_smooth_ += kDelaySlew * (params.time * SAMPLE_RATE - delay_smooth_);
    swell_line_.SetDelay(delay_smooth_);

    // Detect rising edge: envelope crosses threshold upward
    const float level       = follower_.Process(input);
    const bool  now_above   = level > TRIGGER_THRESHOLD;
    const bool  rising_edge = now_above && !prev_above_threshold_;
    prev_above_threshold_   = now_above;

    if (rising_edge) {
        state_ = SwellState::Attack;
    }

    // Advance AD state machine
    switch (state_) {
        case SwellState::Idle:
            break; // env_gain_ is always 0 here

        case SwellState::Attack:
            env_gain_ += attack_rate_;
            if (env_gain_ >= 1.0f) {
                env_gain_ = 1.0f;
                state_    = SwellState::Decay;
            }
            break;

        case SwellState::Decay:
            env_gain_ -= decay_rate_;
            if (env_gain_ <= 0.0f) {
                env_gain_ = 0.0f;
                state_    = SwellState::Idle;
            }
            break;
    }

    float wet = swell_line_.Read() * env_gain_;

    const float feedback = fb_lim_.Process(wet * params.repeats);
    swell_line_.Write(input + feedback);

    wet = dc_.Process(wet);

    return StereoFrame{wet, wet};
}

} // namespace pedal
