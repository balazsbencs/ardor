#pragma once
#include "delay_mode.h"
#include "../dsp/envelope_follower.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

namespace pedal {

class SwellDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Swell"; }

private:
    enum class SwellState { Idle, Attack, Decay };

    EnvelopeFollower follower_;
    ToneFilter       filter_l_;
    ToneFilter       filter_r_;
    DcBlocker        dc_l_;
    DcBlocker        dc_r_;
    FeedbackLimiter  fb_lim_l_;
    FeedbackLimiter  fb_lim_r_;

    SwellState state_    = SwellState::Idle;
    float      env_gain_ = 0.0f;
    float      attack_rate_ = 0.0f;
    float      decay_rate_  = 0.0f;
    float      delay_current_ = -1.0f;
    float      delay_previous_ = -1.0f;
    int        time_crossfade_remaining_ = 0;
    bool       prev_above_threshold_ = false;

    static constexpr float kBaseTriggerThreshold = 0.05f;

    float          swell_buf_l_[MAX_DELAY_SAMPLES];
    float          swell_buf_r_[MAX_DELAY_SAMPLES];
    DelayLineSdram swell_line_l_;
    DelayLineSdram swell_line_r_;
};

} // namespace pedal
