#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"

namespace pedal {

class LofiDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Lofi"; }

private:
    Lfo       lfo_;
    DcBlocker dc_;

    float    held_sample_  = 0.0f;
    float    sr_counter_   = 0.0f;
    float    decimate_     = 1.0f;
    float    bit_scale_    = 65536.0f;
    int      bits_         = 16;

    float    aa_lp_        = 0.0f;
    float    delay_smooth_ = 0.0f;
    FeedbackLimiter fb_lim_;
    DcBlocker  dc_fb_;
};

} // namespace pedal
