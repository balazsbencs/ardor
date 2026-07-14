#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/svf.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"

namespace pedal {

class FilterDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Filter"; }

private:
    Lfo       lfo_;
    Svf       svf_l_;
    Svf       svf_r_;
    DcBlocker dc_l_;
    DcBlocker dc_r_;
    FeedbackLimiter fb_lim_l_;
    FeedbackLimiter fb_lim_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
    float     delay_smooth_l_ = 0.0f;
    float     delay_smooth_r_ = 0.0f;
    float     target_delay_    = 0.0f;
    float     filter_fb_gain_  = 1.0f;
    // Precomputed g = tan(π·f/fs) bounds for LFO sweep — avoids per-sample tanf().
    float     g_lo_ = 0.05f;
    float     g_hi_ = 0.05f;

};

} // namespace pedal
