#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/saturation.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

namespace pedal {

class TapeDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Tape"; }

private:
    Lfo        lfo_;
    ToneFilter filter_l_;
    ToneFilter filter_r_;
    Saturation sat_;
    DcBlocker  dc_l_;
    DcBlocker  dc_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
    float      env_state_l_ = 0.0f;
    float      env_state_r_ = 0.0f;
    float      tape_lp_l_ = 0.0f;
    float      tape_lp_r_ = 0.0f;
    float      delay_smooth_ = -1.0f;
    float aa_state_l_ = 0.0f;
    float aa_state_r_ = 0.0f;
    float aa_coef_  = 1.0f;
    FeedbackLimiter fb_lim_l_;
    FeedbackLimiter fb_lim_r_;
    float pre_shelf_state_l_  = 0.0f;  // HF pre-emphasis state
    float pre_shelf_state_r_  = 0.0f;
    float post_shelf_state_l_ = 0.0f;  // HF de-emphasis state
    float post_shelf_state_r_ = 0.0f;
    float shelf_coef_       = 0.0f;  // 1-pole HP coefficient (~3kHz)
    float shelf_gain_       = 0.0f;  // 0 at grit=0, 1 at grit=1
    float          tape_buf_l_[MAX_DELAY_SAMPLES];
    float          tape_buf_r_[MAX_DELAY_SAMPLES];
    DelayLineSdram tape_line_l_;
    DelayLineSdram tape_line_r_;
};

} // namespace pedal
