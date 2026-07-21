#pragma once
#include "reverb_mode.h"
#include "../config/constants.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"

namespace pedal {

class MagnetoReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Magneto"; }

private:
    static constexpr size_t kMainDelaySize = static_cast<size_t>(REVERB_SAMPLE_RATE * 1.5f) + 16;

    DelayLineSdram delay_l_;
    DelayLineSdram delay_r_;
    Diffuser       diffuser_l_;
    Diffuser       diffuser_r_;
    int            n_heads_        = 4;
    bool           golden_spacing_ = false;
    float          head_delays_[6]{};
    float          fb_lp_l_ = 0.0f;
    float          fb_lp_r_ = 0.0f;

    float buf_main_l_[kMainDelaySize];
    float buf_main_r_[kMainDelaySize];
    float buf_diff_l0_[Diffuser::kDelays[0] + 1];
    float buf_diff_l1_[Diffuser::kDelays[1] + 1];
    float buf_diff_l2_[Diffuser::kDelays[2] + 1];
    float buf_diff_l3_[Diffuser::kDelays[3] + 1];
    float buf_diff_r0_[Diffuser::kDelays[0] + 1];
    float buf_diff_r1_[Diffuser::kDelays[1] + 1];
    float buf_diff_r2_[Diffuser::kDelays[2] + 1];
    float buf_diff_r3_[Diffuser::kDelays[3] + 1];
};

} // namespace pedal
