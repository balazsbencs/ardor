#pragma once
#include "reverb_mode.h"
#include "../config/constants.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"
#include "../dsp/tone_filter.h"

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
    ToneFilter     tone_[2];
    // Head positions move whenever Decay, head count or spacing changes.
    // Prepare() sets the targets; Process() slews the live positions toward
    // them one sample at a time. Jumping straight to a new position steps every
    // head discontinuously each control block, which clicks.
    static constexpr float kHeadSlew = 0.0001f;      // ~0.2 s glide
    static constexpr float kHeadSlewMaxStep = 0.5f;  // samples per sample

    int            n_heads_        = 4;
    bool           golden_spacing_ = false;
    bool           heads_seeded_   = false;
    float          head_target_l_[6]{};
    float          head_target_r_[6]{};
    float          head_delays_l_[6]{};
    float          head_delays_r_[6]{};
    float          fb_lp_l_ = 0.0f;
    float          fb_lp_r_ = 0.0f;

    float buf_main_l_[kMainDelaySize];
    float buf_main_r_[kMainDelaySize];
    float buf_diff_l0_[pedal::diffuser_delays::MAGNETO_L[0] + Diffuser::MOD_HEADROOM];
    float buf_diff_l1_[pedal::diffuser_delays::MAGNETO_L[1] + Diffuser::MOD_HEADROOM];
    float buf_diff_l2_[pedal::diffuser_delays::MAGNETO_L[2] + Diffuser::MOD_HEADROOM];
    float buf_diff_l3_[pedal::diffuser_delays::MAGNETO_L[3] + Diffuser::MOD_HEADROOM];
    float buf_diff_r0_[pedal::diffuser_delays::MAGNETO_R[0] + Diffuser::MOD_HEADROOM];
    float buf_diff_r1_[pedal::diffuser_delays::MAGNETO_R[1] + Diffuser::MOD_HEADROOM];
    float buf_diff_r2_[pedal::diffuser_delays::MAGNETO_R[2] + Diffuser::MOD_HEADROOM];
    float buf_diff_r3_[pedal::diffuser_delays::MAGNETO_R[3] + Diffuser::MOD_HEADROOM];
};

} // namespace pedal
