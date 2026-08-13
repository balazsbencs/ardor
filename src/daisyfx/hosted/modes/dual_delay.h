#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/delay_tap_transition.h"
#include "../config/constants.h"

namespace pedal {

class DualDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Dual"; }

private:
    Lfo        lfo_;
    ToneFilter filter_l_;
    ToneFilter filter_r_;
    DcBlocker  dc_l_;
    DcBlocker  dc_r_;
    DelayTapTransition time_transition_;
    FeedbackLimiter fb_lim_l_;
    FeedbackLimiter fb_lim_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
    static constexpr size_t kDualDelaySamples = static_cast<size_t>(SAMPLE_RATE * 3.8f);
    float          buf_l_[kDualDelaySamples];
    float          buf_r_[kDualDelaySamples];
    DelayLineSdram line_l_;
    DelayLineSdram line_r_;
};

} // namespace pedal
