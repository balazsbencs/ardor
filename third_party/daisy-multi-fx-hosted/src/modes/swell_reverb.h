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
    const char* Name() const override { return "Swell"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram   pre_delay_;
    Fdn              fdn_;
    ToneFilter       tone_[2];
    EnvelopeFollower env_follow_;
    float            ramp_gain_  = 0.0f;
    float            ramp_rate_  = 0.0f;
};

} // namespace pedal
