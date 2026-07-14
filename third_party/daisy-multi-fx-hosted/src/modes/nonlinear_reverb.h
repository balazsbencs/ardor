#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"
#include "../dsp/tone_filter.h"
#include "../dsp/envelope_follower.h"

namespace pedal {

class NonlinearReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Nonlinear"; }

private:
    DelayLineSdram pre_delay_;
    Diffuser       diffuser_;
    Fdn            fdn_;
    ToneFilter     tone_[2];
    EnvelopeFollower input_env_;
    float          shape_phase_ = 0.0f;
    float          input_env_slow_ = 0.0f;
    float          shape_gain_smooth_ = 0.0f;
    float          decay_rate_  = 0.0f;
};

} // namespace pedal
