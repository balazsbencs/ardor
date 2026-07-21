#pragma once
#include "reverb_mode.h"
#include "../dsp/allpass.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/lfo.h"

namespace pedal {

// Dattorro (1997) plate reverb.
// All delay lengths scaled from the original 29761 Hz to 48000 Hz (×1.61289).
class PlateReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Plate"; }
    void SetHold(bool h) override;
    bool SupportsHold() const override { return true; }

private:
    float buf_pre_l_[24001];
    float buf_pre_r_[24001];
    float buf_idif0_[230];
    float buf_idif1_[174];
    float buf_idif2_[612];
    float buf_idif3_[448];
    float buf_ap5_[1098];
    float buf_d5_[7182];
    float buf_ap7_[2904];
    float buf_d6_[6001];
    float buf_ap6_[1480];
    float buf_d7_[6801];
    float buf_ap8_[4285];
    float buf_d8_[5100];

    // Pre-delay (0..500 ms)
    DelayLineSdram pre_delay_l_;
    DelayLineSdram pre_delay_r_;
    size_t         pre_delay_samp_ = 0;

    // Input diffusion: 4 series allpass (k1=0.75, k1=0.75, k2=0.625, k2=0.625)
    DelayAllpassFilter idif_[4];

    // Tank A: AP5(mod) → D5 → LP_A → AP7 → D6
    DelayAllpassFilter ap5_;
    DelayLineSdram     d5_;
    DelayAllpassFilter ap7_;
    DelayLineSdram     d6_;

    // Tank B: AP6(mod) → D7 → LP_B → AP8 → D8
    DelayAllpassFilter ap6_;
    DelayLineSdram     d7_;
    DelayAllpassFilter ap8_;
    DelayLineSdram     d8_;

    // One-pole LP damping state & coefficient
    float lp_a_    = 0.0f;
    float lp_b_    = 0.0f;
    float lp_coef_ = 0.5f;

    // Saved inner allpass outputs — used in the output tap mix
    float last_ap7_ = 0.0f;
    float last_ap8_ = 0.0f;

    // Quadrature LFOs for the modulated decay-diffusers
    Lfo lfo_a_;
    Lfo lfo_b_;

    // Input diffuser gains (Param2-controlled)
    float in_g_hi_ = 0.75f;   // 0.65 – 0.80 (higher gain)
    float in_g_lo_ = 0.625f;  // 0.50 – 0.65 (lower gain)

    float decay_     = 0.5f;
    float mod_depth_ = 13.0f;  // LFO depth in samples; set from param1 in Prepare()
    bool  hold_      = false;
};

} // namespace pedal
