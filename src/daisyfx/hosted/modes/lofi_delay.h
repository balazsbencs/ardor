#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

namespace pedal {

class LofiDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Lofi"; }

private:
    Lfo       lfo_;
    DcBlocker dc_l_;
    DcBlocker dc_r_;

    float    held_sample_l_ = 0.0f;
    float    held_sample_r_ = 0.0f;
    float    sr_counter_   = 0.0f;
    float    decimate_     = 1.0f;
    float    bit_scale_    = 65536.0f;
    int      bits_         = 16;

    float    aa_lp_l_      = 0.0f;
    float    aa_lp_r_      = 0.0f;
    float    delay_current_ = -1.0f;
    float    delay_previous_ = -1.0f;
    int      time_crossfade_remaining_ = 0;
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
