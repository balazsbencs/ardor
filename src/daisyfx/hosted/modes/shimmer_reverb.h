#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"
#include "../dsp/pitch_shifter.h"
#include "../dsp/tone_filter.h"

namespace pedal {

class ShimmerReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Shimmer"; }
    void SetHold(bool h) override;
    bool SupportsHold() const override { return true; }

private:
    float          buf_pre_delay_l_[24000];
    float          buf_pre_delay_r_[24000];
    float          buf_diff_l0_[pedal::diffuser_delays::SHIMMER_L[0] + Diffuser::MOD_HEADROOM];
    float          buf_diff_l1_[pedal::diffuser_delays::SHIMMER_L[1] + Diffuser::MOD_HEADROOM];
    float          buf_diff_l2_[pedal::diffuser_delays::SHIMMER_L[2] + Diffuser::MOD_HEADROOM];
    float          buf_diff_l3_[pedal::diffuser_delays::SHIMMER_L[3] + Diffuser::MOD_HEADROOM];
    float          buf_diff_r0_[pedal::diffuser_delays::SHIMMER_R[0] + Diffuser::MOD_HEADROOM];
    float          buf_diff_r1_[pedal::diffuser_delays::SHIMMER_R[1] + Diffuser::MOD_HEADROOM];
    float          buf_diff_r2_[pedal::diffuser_delays::SHIMMER_R[2] + Diffuser::MOD_HEADROOM];
    float          buf_diff_r3_[pedal::diffuser_delays::SHIMMER_R[3] + Diffuser::MOD_HEADROOM];
    float          buf_fdn0_[2730];
    float          buf_fdn1_[3252];
    float          buf_fdn2_[3864];
    float          buf_fdn3_[4508];
    float          buf_pitch0_[8192];
    float          buf_pitch1_[8192];
    DelayLineSdram pre_delay_l_;
    DelayLineSdram pre_delay_r_;
    Diffuser       diffuser_l_;
    Diffuser       diffuser_r_;
    Fdn            fdn_;
    PitchShifter   pitch_shifter_[2];
    ToneFilter     tone_[2];
    bool           hold_            = false;
    float          pitch_fb_l_      = 0.0f;  // one-sample-delayed shimmer feedback left
    float          pitch_fb_r_      = 0.0f;  // one-sample-delayed shimmer feedback right
};

} // namespace pedal
