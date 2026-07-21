// Dattorro (1997) plate reverb.
// Reference: Jon Dattorro, "Effect Design Part 1: Reverberator and Other Filters",
// JAES Vol. 45 No. 9, September 1997.
// All integer delay lengths are from the original 29761 Hz design, scaled ×1.61289
// to match our 48000 Hz sample rate.

#include "plate_reverb.h"
#include "../config/constants.h"
#include <cmath>
#include <algorithm>

using namespace pedal::reverb_fx;

namespace pedal {

// ---------------------------------------------------------------------------
// Delay / tap constants (originally scaled to 48 kHz, executed by the 24 kHz
// reverb stage).
// ---------------------------------------------------------------------------
namespace {

// Input diffuser delays
constexpr size_t kIdif0 = 115;
constexpr size_t kIdif1 = 87;
constexpr size_t kIdif2 = 306;
constexpr size_t kIdif3 = 224;

// AP5/AP6 modulated allpass centre delays; LFO depth set per-block from param1
constexpr float kAp5Centre  = 542.0f;
constexpr float kAp6Centre  = 732.5f;

// Tank A delays
constexpr float kD5Delay = 3590.5f;
constexpr size_t kAp7    = 1452;
constexpr float kD6Delay = 3000.0f;

// Tank B delays
constexpr float kD7Delay = 3400.0f;
constexpr size_t kAp8    = 2142;
constexpr float kD8Delay = 2549.5f;

// Output tap read positions — read BEFORE the corresponding Write() this sample.
// Left out  = +D5[429] + D5[4797] – AP7_out – D6[3086] + D7[3210] – D7[302]  – AP8_out
// Right out = +D7[569] + D7[5852] – AP8_out – D8[1981] + D5[4311] – D5[3210] – AP7_out
constexpr float kD5TapL0 = 214.5f;
constexpr float kD5TapL1 = 2398.5f;
constexpr float kD6TapL  = 1543.0f;
constexpr float kD7TapL0 = 1605.0f;
constexpr float kD7TapL1 = 151.0f;

constexpr float kD7TapR0 = 284.5f;
constexpr float kD7TapR1 = 2926.0f;
constexpr float kD8TapR  = 990.5f;
constexpr float kD5TapR0 = 2155.5f;
constexpr float kD5TapR1 = 1605.0f;

// Output normalisation: sum of 7 taps per channel, empirically tuned to ~0dB
constexpr float kOutGain = 0.35f;

// RT60 -> decay: use the actual reverb processing rate.
constexpr float kAvgTankSec = (kD5Delay + kD6Delay + kD7Delay + kD8Delay) / (4.0f * REVERB_SAMPLE_RATE);

} // namespace

// ---------------------------------------------------------------------------
void PlateReverb::Init() {
    pre_delay_l_.Init(buf_pre_l_, 24001);
    pre_delay_r_.Init(buf_pre_r_, 24001);

    idif_[0].Init(buf_idif0_, 230);  idif_[0].SetDelay(kIdif0);
    idif_[1].Init(buf_idif1_, 174);  idif_[1].SetDelay(kIdif1);
    idif_[2].Init(buf_idif2_, 612);  idif_[2].SetDelay(kIdif2);
    idif_[3].Init(buf_idif3_, 448);  idif_[3].SetDelay(kIdif3);

    ap5_.Init(buf_ap5_, 1098);
    d5_ .Init(buf_d5_,  7182);
    ap7_.Init(buf_ap7_, 2904);  ap7_.SetDelay(kAp7);
    d6_ .Init(buf_d6_,  6001);

    ap6_.Init(buf_ap6_, 1480);
    d7_ .Init(buf_d7_,  6801);
    ap8_.Init(buf_ap8_, 4285);  ap8_.SetDelay(kAp8);
    d8_ .Init(buf_d8_,  5100);

    // Quadrature LFOs: A at 0°, B at 90°
    lfo_a_.Init(1.0f, LfoWave::Sine, REVERB_SAMPLE_RATE);
    lfo_b_.Init(1.0f, LfoWave::Sine, REVERB_SAMPLE_RATE);
    lfo_b_.SetPhaseOffset(1.5707963f);  // π/2

    lp_a_ = lp_b_ = 0.0f;
    last_ap7_ = last_ap8_ = 0.0f;
    pre_delay_samp_ = 0;
    decay_ = 0.5f;
    hold_  = false;
}

void PlateReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    for (auto& a : idif_) a.Reset();
    ap5_.Reset();  d5_.Reset();  ap7_.Reset();  d6_.Reset();
    ap6_.Reset();  d7_.Reset();  ap8_.Reset();  d8_.Reset();
    lfo_a_.Reset();
    lfo_b_.Reset();
    lp_a_ = lp_b_ = 0.0f;
    last_ap7_ = last_ap8_ = 0.0f;
    hold_ = false;
}

void PlateReverb::Prepare(const ParamSet& params) {
    pre_delay_samp_ = static_cast<size_t>(params.pre_delay * REVERB_SAMPLE_RATE);
    if (pre_delay_samp_ >= 24000) pre_delay_samp_ = 23999;

    // RT60 → feedback coefficient
    decay_ = hold_ ? 1.0f
                   : std::min(0.97f, std::exp(-6.9078f * kAvgTankSec / params.decay));

    // One-pole LP coefficient: tone=0 → dark (0.90), tone=1 → bright (0.05)
    lp_coef_ = 0.90f - params.tone * 0.85f;

    // LFO rate from mod param: 0.3..2.0 Hz
    const float mod_rate = 0.3f + params.mod * 1.7f;
    // Param1: 0 = tight studio plate (4 samples), 1 = lush shimmer (20 samples)
    mod_depth_ = 4.0f + params.param1 * 16.0f;
    lfo_a_.SetRate(mod_rate * 2.0f);
    lfo_b_.SetRate(mod_rate * 2.0f);

    // Param2: 0 = smooth (lower g, more transparent), 1 = raw (higher g, more direct)
    in_g_hi_ = 0.65f + params.param2 * 0.15f;  // 0.65 – 0.80
    in_g_lo_ = 0.50f + params.param2 * 0.15f;  // 0.50 – 0.65
}

StereoFrame PlateReverb::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame PlateReverb::Process(StereoFrame input, const ParamSet& params) {
    // --- Pre-delay ---
    const float pre_l = pre_delay_l_.ReadAt(static_cast<float>(pre_delay_samp_ + 1));
    const float pre_r = pre_delay_r_.ReadAt(static_cast<float>(pre_delay_samp_ + 1));
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);

    const float mid = 0.5f * (pre_l + pre_r);
    const float side = 0.5f * (pre_l - pre_r);

    // --- Input diffusion (4 series Schroeder allpasses) ---
    float s = mid;
    s = idif_[0].Process(s, in_g_hi_);
    s = idif_[1].Process(s, in_g_hi_);
    s = idif_[2].Process(s, in_g_lo_);
    s = idif_[3].Process(s, in_g_lo_);

    // --- Read all output & feedback taps BEFORE any tank writes this sample ---
    // Feedback cross-coupling
    const float d6_fb = d6_.ReadAt(kD6Delay);
    const float d8_fb = d8_.ReadAt(kD8Delay);

    // Output taps — left channel
    const float d5_l0 = d5_.ReadAt(kD5TapL0);
    const float d5_l1 = d5_.ReadAt(kD5TapL1);
    const float d6_l  = d6_.ReadAt(kD6TapL);
    const float d7_l0 = d7_.ReadAt(kD7TapL0);
    const float d7_l1 = d7_.ReadAt(kD7TapL1);

    // Output taps — right channel
    const float d7_r0 = d7_.ReadAt(kD7TapR0);
    const float d7_r1 = d7_.ReadAt(kD7TapR1);
    const float d8_r  = d8_.ReadAt(kD8TapR);
    const float d5_r0 = d5_.ReadAt(kD5TapR0);
    const float d5_r1 = d5_.ReadAt(kD5TapR1);

    // --- Tank A: (s + decay*D8) → AP5(mod) → D5 → LP_A → AP7 → D6 ---
    {
        const float lfo_a   = lfo_a_.Process();
        const float ap5_d   = kAp5Centre + mod_depth_ * lfo_a;
        const float ta_in   = (hold_ ? 0.0f : s + side) + decay_ * d8_fb;
        const float y_ap5   = ap5_.ProcessMod(ta_in, 0.70f, ap5_d);
        d5_.Write(y_ap5);

        // Read main D5 output (written this sample → already at ReadAt(1); full delay reads past it)
        const float d5_out  = d5_.ReadAt(kD5Delay);
        lp_a_ = (1.0f - lp_coef_) * d5_out + lp_coef_ * lp_a_;

        last_ap7_ = ap7_.Process(decay_ * lp_a_, 0.50f);
        d6_.Write(last_ap7_);
    }

    // --- Tank B: (s + decay*D6) → AP6(mod) → D7 → LP_B → AP8 → D8 ---
    {
        const float lfo_b   = lfo_b_.Process();
        const float ap6_d   = kAp6Centre + mod_depth_ * lfo_b;
        const float tb_in   = (hold_ ? 0.0f : s - side) + decay_ * d6_fb;
        const float y_ap6   = ap6_.ProcessMod(tb_in, 0.70f, ap6_d);
        d7_.Write(y_ap6);

        const float d7_out  = d7_.ReadAt(kD7Delay);
        lp_b_ = (1.0f - lp_coef_) * d7_out + lp_coef_ * lp_b_;

        last_ap8_ = ap8_.Process(decay_ * lp_b_, 0.50f);
        d8_.Write(last_ap8_);
    }

    // --- Output tap mix (Dattorro Figure 13) ---
    const float wet_l = kOutGain * (  d5_l0 + d5_l1 - last_ap7_
                                     - d6_l  + d7_l0 - d7_l1 - last_ap8_);
    const float wet_r = kOutGain * (  d7_r0 + d7_r1 - last_ap8_
                                     - d8_r  + d5_r0 - d5_r1 - last_ap7_);

    return StereoFrame{ wet_l, wet_r };
}

void PlateReverb::SetHold(bool h) {
    hold_  = h;
    decay_ = h ? 1.0f : decay_;
}

} // namespace pedal
