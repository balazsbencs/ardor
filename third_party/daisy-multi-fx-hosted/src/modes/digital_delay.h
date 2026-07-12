#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"

namespace pedal {

class DigitalDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Digital"; }

private:
    Lfo        lfo_;
    ToneFilter filter_l_;
    ToneFilter filter_r_;
    DcBlocker  dc_l_;
    DcBlocker  dc_r_;
    float aa_state_l_ = 0.0f;  // anti-alias LP state for L write
    float aa_state_r_ = 0.0f;  // anti-alias LP state for R write
    float aa_coef_    = 1.0f;  // LP coefficient (1.0 = bypass)
    float      delay_smooth_l_ = 0.0f;
    float      delay_smooth_r_ = 0.0f;
    FeedbackLimiter fb_lim_l_;
    FeedbackLimiter fb_lim_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
};

} // namespace pedal
