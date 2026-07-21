#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/fdn.h"
#include "../dsp/tone_filter.h"
#include "../dsp/envelope_follower.h"

namespace pedal {

class SwellReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Swell"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    float            buf_pre_delay_l_[24000];
    float            buf_pre_delay_r_[24000];
    float            buf_fdn0_[2522];
    float            buf_fdn1_[3080];
    float            buf_fdn2_[3660];
    float            buf_fdn3_[4232];
    DelayLineSdram   pre_delay_l_;
    DelayLineSdram   pre_delay_r_;
    Fdn              fdn_;
    ToneFilter       tone_[2];
    EnvelopeFollower env_follow_;
    float            ramp_gain_  = 0.0f;
    float            ramp_rate_  = 0.0f;
    bool             swell_dry_  = true;
};

} // namespace pedal
