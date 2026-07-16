#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

namespace pedal {

class DualDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Dual"; }

private:
    Lfo        lfo_;
    ToneFilter filter_l_;
    ToneFilter filter_r_;
    DcBlocker  dc_l_;
    DcBlocker  dc_r_;
    float      delay_smooth_l_ = 0.0f;
    float      delay_smooth_r_ = 0.0f;
    FeedbackLimiter fb_lim_l_;
    FeedbackLimiter fb_lim_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
    float          buf_l_[MAX_DELAY_SAMPLES];
    float          buf_r_[MAX_DELAY_SAMPLES];
    DelayLineSdram line_l_;
    DelayLineSdram line_r_;
};

} // namespace pedal
