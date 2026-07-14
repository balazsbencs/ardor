#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"
#include "../dsp/tone_filter.h"
#include "../dsp/envelope_follower.h"

namespace pedal {

class BloomReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Bloom"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram pre_delay_;
    Diffuser       diffuser_;
    Fdn            fdn_;
    ToneFilter     tone_[2];
    EnvelopeFollower input_env_;
    float          bloom_env_         = 0.0f;
    float          input_env_slow_    = 0.0f;
    float          bloom_rate_        = 0.0001f;
    float          bloom_feedback_    = 0.0f;   // param2-derived amount
    float          bloom_fb_signal_   = 0.0f;   // previous output fed back
};

} // namespace pedal
