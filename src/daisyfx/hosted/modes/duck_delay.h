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
    const char* Name() const override { return "Duck"; }

private:
    Lfo              lfo_;
    EnvelopeFollower follower_;
    ToneFilter       filter_;
    DcBlocker        dc_;
    float            delay_smooth_ = 0.0f;
    FeedbackLimiter  fb_lim_;
    float            duck_buf_[MAX_DELAY_SAMPLES];
    DelayLineSdram   duck_line_;
};

} // namespace pedal
