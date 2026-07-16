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
    const char* Name() const override { return "Hall"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram  pre_delay_;
    EarlyReflections er_;
    Diffuser         diffuser_;
    Fdn              fdn_;

    float buf_pre_delay_[24000];
    float buf_er_[4096];
    float buf_diff0_[Diffuser::kDelays[0] + 1];
    float buf_diff1_[Diffuser::kDelays[1] + 1];
    float buf_diff2_[Diffuser::kDelays[2] + 1];
    float buf_diff3_[Diffuser::kDelays[3] + 1];
    float buf_fdn0_[3307];
    float buf_fdn1_[4159];
    float buf_fdn2_[5903];
    float buf_fdn3_[6997];
};

} // namespace pedal
