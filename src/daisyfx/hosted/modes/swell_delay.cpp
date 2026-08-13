#include "swell_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 47.0f;

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
    time_transition_.Reset();
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

    time_transition_.SetTarget(params.time * SAMPLE_RATE);
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

    const auto readHeads = [&](float base) {
        return StereoFrame{swell_line_l_.ReadNearest(base),
                           swell_line_r_.ReadNearest(base + kStereoOffsetSamples)};
    };
    StereoFrame wet = readHeads(time_transition_.to());
    if (time_transition_.active()) {
        const StereoFrame old = readHeads(time_transition_.from());
        const float fade = time_transition_.mix();
        wet.left = old.left + fade * (wet.left - old.left);
        wet.right = old.right + fade * (wet.right - old.right);
        time_transition_.Advance();
    }
    float wet_l = wet.left;
    float wet_r = wet.right;
    wet_l = filter_l_.Process(wet_l) * env_gain_;
    wet_r = filter_r_.Process(wet_r) * env_gain_;

    const float feedback_l = fb_lim_l_.Process(wet_l * params.repeats);
    const float feedback_r = fb_lim_r_.Process(wet_r * params.repeats);
    swell_line_l_.Write(input.left + feedback_l);
    swell_line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
