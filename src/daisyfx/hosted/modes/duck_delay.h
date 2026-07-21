#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/envelope_follower.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

namespace pedal {

class DuckDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Duck"; }

private:
    Lfo              lfo_;
    EnvelopeFollower follower_;
    ToneFilter       filter_l_;
    ToneFilter       filter_r_;
    DcBlocker        dc_l_;
    DcBlocker        dc_r_;
    float            delay_current_ = -1.0f;
    float            delay_previous_ = -1.0f;
    int              time_crossfade_remaining_ = 0;
    FeedbackLimiter  fb_lim_l_;
    FeedbackLimiter  fb_lim_r_;
    float            duck_buf_l_[MAX_DELAY_SAMPLES];
    float            duck_buf_r_[MAX_DELAY_SAMPLES];
    DelayLineSdram   duck_line_l_;
    DelayLineSdram   duck_line_r_;
};

} // namespace pedal
