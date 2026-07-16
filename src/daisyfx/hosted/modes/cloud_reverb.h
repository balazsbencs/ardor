#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"
#include "../dsp/tone_filter.h"

namespace pedal {

class CloudReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Cloud"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram pre_delay_;
    Diffuser       diffuser0_;
    Diffuser       diffuser1_;
    Fdn            fdn_;
    ToneFilter     tone_[2];

    float buf_pre_delay_[24000];
    float buf_d0_0_[Diffuser::kDelays[0] + 1];
    float buf_d0_1_[Diffuser::kDelays[1] + 1];
    float buf_d0_2_[Diffuser::kDelays[2] + 1];
    float buf_d0_3_[Diffuser::kDelays[3] + 1];
    float buf_d1_0_[Diffuser::kDelays[0] + 1];
    float buf_d1_1_[Diffuser::kDelays[1] + 1];
    float buf_d1_2_[Diffuser::kDelays[2] + 1];
    float buf_d1_3_[Diffuser::kDelays[3] + 1];
    float buf_fdn0_[4802];
    float buf_fdn1_[6152];
    float buf_fdn2_[7700];
    float buf_fdn3_[9002];
};

} // namespace pedal
