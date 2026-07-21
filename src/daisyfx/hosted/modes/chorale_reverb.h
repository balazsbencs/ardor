#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/fdn.h"
#include "../dsp/tone_filter.h"
#include "../dsp/formant_filter.h"

namespace pedal {

class ChoraleReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Chorale"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram pre_delay_l_;
    DelayLineSdram pre_delay_r_;
    FormantFilter  formant_l_;
    FormantFilter  formant_r_;
    Fdn            fdn_;
    ToneFilter     tone_[2];
    int            resonance_mode_ = 1;

    float buf_pre_delay_l_[24000];
    float buf_pre_delay_r_[24000];
    float buf_fdn0_[2732];
    float buf_fdn1_[3252];
    float buf_fdn2_[3878];
    float buf_fdn3_[4494];
};

} // namespace pedal
