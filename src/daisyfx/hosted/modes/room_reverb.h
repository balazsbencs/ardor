#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/early_reflections.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"

namespace pedal {

class RoomReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Room"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    float buf_pre_delay_l_[24000];
    float buf_pre_delay_r_[24000];
    float buf_er_l_[4096];
    float buf_er_r_[4096];
    float buf_diff_l0_[Diffuser::kDelays[0] + 1];
    float buf_diff_l1_[Diffuser::kDelays[1] + 1];
    float buf_diff_l2_[Diffuser::kDelays[2] + 1];
    float buf_diff_l3_[Diffuser::kDelays[3] + 1];
    float buf_diff_r0_[Diffuser::kDelays[0] + 1];
    float buf_diff_r1_[Diffuser::kDelays[1] + 1];
    float buf_diff_r2_[Diffuser::kDelays[2] + 1];
    float buf_diff_r3_[Diffuser::kDelays[3] + 1];
    float buf_fdn0_[1907];
    float buf_fdn1_[2593];
    float buf_fdn2_[3697];
    float buf_fdn3_[4799];
    DelayLineSdram pre_delay_l_;
    DelayLineSdram pre_delay_r_;
    EarlyReflections er_l_;
    EarlyReflections er_r_;
    Diffuser diffuser_l_;
    Diffuser diffuser_r_;
    Fdn fdn_;
};

} // namespace pedal
