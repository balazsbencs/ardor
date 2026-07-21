#include "swell_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

void SwellDelay::Init() {
    swell_line_l_.Init(swell_buf_l_, MAX_DELAY_SAMPLES);
    swell_line_r_.Init(swell_buf_r_, MAX_DELAY_SAMPLES);
    follower_.Init(5.0f, 80.0f);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    dc_l_.Init();
    dc_r_.Init();
    state_                = SwellState::Idle;
    env_gain_             = 0.0f;
    prev_above_threshold_ = false;
}

void SwellDelay::Reset() {
    swell_line_l_.Reset();
    swell_line_r_.Reset();
    follower_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    state_                = SwellState::Idle;
    env_gain_             = 0.0f;
    attack_rate_          = 0.0f;
    decay_rate_           = 0.0f;
    delay_current_        = -1.0f;
    delay_previous_       = -1.0f;
    time_crossfade_remaining_ = 0;
    prev_above_threshold_ = false;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
}

void SwellDelay::Prepare(const ParamSet& params) {
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
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

    const float targetDelay = params.time * SAMPLE_RATE;
    if (delay_current_ < 0.0f) {
        delay_current_ = targetDelay;
        delay_previous_ = targetDelay;
    } else if (fabsf(targetDelay - delay_current_) > 0.01f) {
        delay_previous_ = delay_current_;
        delay_current_ = targetDelay;
        time_crossfade_remaining_ = kTimeCrossfadeSamples;
    }
}

StereoFrame SwellDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame SwellDelay::Process(StereoFrame input, const ParamSet& params) {
    // Detect rising edge: envelope crosses threshold upward
    const float detector    = fmaxf(fabsf(input.left), fabsf(input.right));
    const float level       = follower_.Process(detector);
    // Grit is exposed by this mode as Threshold. Keep the established 0.05
    // trigger at zero, then raise it to make the swell progressively less
    // sensitive to quiet notes and pickup noise.
    const float triggerThreshold = kBaseTriggerThreshold + params.grit * 0.20f;
    const bool  now_above   = level > triggerThreshold;
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

    float wet_l = swell_line_l_.ReadAt(delay_current_);
    float wet_r = swell_line_r_.ReadAt(delay_current_);
    if (time_crossfade_remaining_ > 0) {
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float previousWetL = swell_line_l_.ReadAt(delay_previous_);
        const float previousWetR = swell_line_r_.ReadAt(delay_previous_);
        wet_l = previousWetL + fade * (wet_l - previousWetL);
        wet_r = previousWetR + fade * (wet_r - previousWetR);
        --time_crossfade_remaining_;
    }
    wet_l = filter_l_.Process(wet_l) * env_gain_;
    wet_r = filter_r_.Process(wet_r) * env_gain_;

    const float feedback_l = fb_lim_l_.Process(wet_l * params.repeats);
    const float feedback_r = fb_lim_r_.Process(wet_r * params.repeats);
    swell_line_l_.Write(input.left + feedback_l);
    swell_line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
