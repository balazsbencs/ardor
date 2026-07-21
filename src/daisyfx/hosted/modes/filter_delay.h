#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/svf.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

namespace pedal {

class FilterDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Filter"; }

private:
    enum class FilterType { Lowpass, Bandpass, Highpass };
    Lfo       lfo_;
    Svf       svf_l_;
    Svf       svf_r_;
    DcBlocker dc_l_;
    DcBlocker dc_r_;
    FeedbackLimiter fb_lim_l_;
    FeedbackLimiter fb_lim_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
    float     delay_current_ = -1.0f;
    float     delay_previous_ = -1.0f;
    int       time_crossfade_remaining_ = 0;
    FilterType filter_type_ = FilterType::Lowpass;
    float     filter_fb_gain_  = 1.0f;
    // Precomputed g = tan(π·f/fs) bounds for LFO sweep — avoids per-sample tanf().
    float     g_lo_ = 0.05f;
    float     g_hi_ = 0.05f;

    float          filter_buf_l_[MAX_DELAY_SAMPLES];
    float          filter_buf_r_[MAX_DELAY_SAMPLES];
    DelayLineSdram filter_line_l_;
    DelayLineSdram filter_line_r_;
};

} // namespace pedal
