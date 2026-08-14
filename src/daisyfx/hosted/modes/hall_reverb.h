#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/early_reflections.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"

namespace pedal {

class HallReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Hall"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram  pre_delay_l_;
    DelayLineSdram  pre_delay_r_;
    EarlyReflections er_l_;
    EarlyReflections er_r_;
    Diffuser         diffuser_l_;
    Diffuser         diffuser_r_;
    Fdn              fdn_;
    float            mid_fast_[2]{};
    float            mid_slow_[2]{};
    float            mid_scale_ = 0.0f;

    float buf_pre_delay_l_[24000];
    float buf_pre_delay_r_[24000];
    float buf_er_l_[4096];
    float buf_er_r_[4096];
    float buf_diff_l0_[pedal::diffuser_delays::HALL_L[0] + Diffuser::MOD_HEADROOM];
    float buf_diff_l1_[pedal::diffuser_delays::HALL_L[1] + Diffuser::MOD_HEADROOM];
    float buf_diff_l2_[pedal::diffuser_delays::HALL_L[2] + Diffuser::MOD_HEADROOM];
    float buf_diff_l3_[pedal::diffuser_delays::HALL_L[3] + Diffuser::MOD_HEADROOM];
    float buf_diff_r0_[pedal::diffuser_delays::HALL_R[0] + Diffuser::MOD_HEADROOM];
    float buf_diff_r1_[pedal::diffuser_delays::HALL_R[1] + Diffuser::MOD_HEADROOM];
    float buf_diff_r2_[pedal::diffuser_delays::HALL_R[2] + Diffuser::MOD_HEADROOM];
    float buf_diff_r3_[pedal::diffuser_delays::HALL_R[3] + Diffuser::MOD_HEADROOM];
    float buf_fdn0_[3307];
    float buf_fdn4_[3663];
    float buf_fdn1_[4159];
    float buf_fdn5_[4787];
    float buf_fdn2_[5903];
    float buf_fdn6_[6443];
    float buf_fdn3_[6997];
    float buf_fdn7_[7815];
};

} // namespace pedal
