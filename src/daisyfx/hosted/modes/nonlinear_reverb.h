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
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Nonlinear"; }

private:
    float          buf_pre_delay_l_[24000];
    float          buf_pre_delay_r_[24000];
    float          buf_diff_l0_[Diffuser::kDelays[0] + 1];
    float          buf_diff_l1_[Diffuser::kDelays[1] + 1];
    float          buf_diff_l2_[Diffuser::kDelays[2] + 1];
    float          buf_diff_l3_[Diffuser::kDelays[3] + 1];
    float          buf_diff_r0_[Diffuser::kDelays[0] + 1];
    float          buf_diff_r1_[Diffuser::kDelays[1] + 1];
    float          buf_diff_r2_[Diffuser::kDelays[2] + 1];
    float          buf_diff_r3_[Diffuser::kDelays[3] + 1];
    float          buf_fdn0_[1452];
    float          buf_fdn1_[1748];
    float          buf_fdn2_[2084];
    float          buf_fdn3_[2412];
    DelayLineSdram pre_delay_l_;
    DelayLineSdram pre_delay_r_;
    Diffuser       diffuser_l_;
    Diffuser       diffuser_r_;
    Fdn            fdn_;
    ToneFilter     tone_[2];
    EnvelopeFollower input_env_;
    float          shape_phase_ = 0.0f;
    float          input_env_slow_ = 0.0f;
    float          shape_gain_smooth_ = 0.0f;
    float          decay_rate_  = 0.0f;
    int            shape_ = 3;
};

} // namespace pedal
