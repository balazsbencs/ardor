#include "spring_reverb.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

// Base allpass delays (spring 0)
static constexpr size_t kApDelays0[6] = { 85, 115, 155, 215, 295, 395 };
// g values descending
static constexpr float  kApG[6]       = { 0.70f, 0.65f, 0.60f, 0.55f, 0.50f, 0.45f };
// Spring 1 allpass delays (×1.07, rounded)
static constexpr size_t kApDelays1[6] = { 91, 124, 166, 231, 316, 423 };
// Spring 2 allpass delays (×1.13, rounded)
static constexpr size_t kApDelays2[6] = { 96, 130, 175, 243, 334, 447 };

void SpringReverb::Init() {
    // Spring 0
    float* sp0_bufs[6] = { s0_ap0_, s0_ap1_, s0_ap2_, s0_ap3_, s0_ap4_, s0_ap5_ };
    const size_t sp0_sizes[6] = { 171, 231, 311, 431, 591, 795 };
    for (int s = 0; s < 6; ++s) {
        ap_[0][s].Init(sp0_bufs[s], sp0_sizes[s]);
        ap_[0][s].SetDelay(kApDelays0[s]);
    }
    comb_[0].Init(s0_comb_, 4001);
    comb_[0].SetDelay(2000);

    // Spring 1
    float* sp1_bufs[6] = { s1_ap0_, s1_ap1_, s1_ap2_, s1_ap3_, s1_ap4_, s1_ap5_ };
    const size_t sp1_sizes[6] = { 183, 248, 333, 462, 633, 850 };
    for (int s = 0; s < 6; ++s) {
        ap_[1][s].Init(sp1_bufs[s], sp1_sizes[s]);
        ap_[1][s].SetDelay(kApDelays1[s]);
    }
    comb_[1].Init(s1_comb_, 4281);
    comb_[1].SetDelay(2140);

    // Spring 2
    float* sp2_bufs[6] = { s2_ap0_, s2_ap1_, s2_ap2_, s2_ap3_, s2_ap4_, s2_ap5_ };
    const size_t sp2_sizes[6] = { 193, 261, 351, 487, 668, 898 };
    for (int s = 0; s < 6; ++s) {
        ap_[2][s].Init(sp2_bufs[s], sp2_sizes[s]);
        ap_[2][s].SetDelay(kApDelays2[s]);
    }
    comb_[2].Init(s2_comb_, 4521);
    comb_[2].SetDelay(2260);

    sat_.Init();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    hold_ = false;
    active_springs_ = 1;
    for (auto& fb : comb_fb_) fb = 0.8f;
    for (auto& makeup : comb_makeup_) makeup = 0.6f;

    spring_lfo_[0].Init(0.60f, LfoWave::SmoothRandom, REVERB_SAMPLE_RATE);
    spring_lfo_[1].Init(0.74f, LfoWave::SmoothRandom, REVERB_SAMPLE_RATE);
    spring_lfo_[2].Init(0.88f, LfoWave::SmoothRandom, REVERB_SAMPLE_RATE);
    spring_lfo_[0].SetJitter(0.3f);
    spring_lfo_[1].SetJitter(0.3f);
    spring_lfo_[2].SetJitter(0.3f);
}

void SpringReverb::Reset() {
    for (int sp = 0; sp < 3; ++sp) {
        for (int s = 0; s < 6; ++s) ap_[sp][s].Reset();
        comb_[sp].Reset();
    }
    // DelayLineSdram::Reset() resets delay_ to 2; re-apply configured delays.
    for (int s = 0; s < 6; ++s) {
        ap_[0][s].SetDelay(kApDelays0[s]);
        ap_[1][s].SetDelay(kApDelays1[s]);
        ap_[2][s].SetDelay(kApDelays2[s]);
    }
    comb_[0].SetDelay(2000);
    comb_[1].SetDelay(2140);
    comb_[2].SetDelay(2260);
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    hold_ = false;
    active_springs_ = 1;
    spring_lfo_[0].Reset();
    spring_lfo_[1].Reset();
    spring_lfo_[2].Reset();
}

void SpringReverb::Prepare(const ParamSet& params) {
    // Comb feedback from decay: g = exp(-6.9078 * comb_delay_s / decay_s)
    // Comb delays execute in the 24 kHz reverb stage.
    static constexpr float kCombDelayS[3] = {
        2000.0f / REVERB_SAMPLE_RATE,
        2140.0f / REVERB_SAMPLE_RATE,
        2260.0f / REVERB_SAMPLE_RATE,
    };
    const float decay = params.decay < 0.01f ? 0.01f : params.decay;
    for (int sp = 0; sp < 3; ++sp) {
        const float nominal_fb = std::exp(-6.9078f * kCombDelayS[sp] / decay);
        comb_fb_[sp] = hold_ ? 1.0f : nominal_fb;
        float makeup = sqrtf(1.0f - nominal_fb * nominal_fb);
        if (makeup < 0.12f) makeup = 0.12f;
        comb_makeup_[sp] = makeup;
    }

    const float damp = 0.15f + params.tone * 0.3f;
    for (int sp = 0; sp < 3; ++sp) {
        comb_[sp].SetFeedback(comb_fb_[sp]);
        comb_[sp].SetDamping(damp);
    }

    // Saturation drive from param1 (Dwell)
    // 0→1.0, 0.25→2.0, 0.5→4.0, 0.75→8.0 → desired_drive = 2^(3*param1)
    // SetDrive(x) sets drive_ = 1 + x*x*15 (quadratic), so x = sqrt((desired_drive - 1) / 15)
    const float desired_drive = std::exp(params.param1 * 3.0f * 0.693147f); // 2^(3*p1)
    sat_.SetDrive(sqrtf((desired_drive - 1.0f) * (1.0f / 15.0f)));

    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    mod_depth_ = params.mod * 4.0f;  // 0 = no modulation, 1 = ±4 samples wobble

    // Keep a small dead band around the spring-count boundaries so automated
    // controls cannot repeatedly add/remove an entire resonator path.
    if (active_springs_ == 1) {
        if (params.param2 > 0.36f) active_springs_ = 2;
    } else if (active_springs_ == 2) {
        if (params.param2 < 0.30f) active_springs_ = 1;
        else if (params.param2 > 0.69f) active_springs_ = 3;
    } else {
        if (params.param2 < 0.63f) active_springs_ = 2;
    }
}

StereoFrame SpringReverb::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame SpringReverb::Process(StereoFrame input, const ParamSet& params) {

    // Determine active springs from param2
    const int n_springs = active_springs_;

    float out_l = 0.0f;
    float out_r = 0.0f;

    for (int sp = 0; sp < n_springs; ++sp) {
        // Allpass dispersion chain
        // Per-spring delay table pointer for modulated last stage
        const size_t* ap_delays = (sp == 0) ? kApDelays0 : (sp == 1) ? kApDelays1 : kApDelays2;

        // The three physical-model paths double as a lightweight input matrix:
        // L, R, then mid. For mono input this exactly reduces to the previous
        // excitation, while anti-phase content no longer cancels at the input.
        const float source = sp == 0 ? input.left
                           : sp == 1 ? input.right
                                     : 0.5f * (input.left + input.right);
        float s = sat_.Process(source);
        for (int st = 0; st < 5; ++st) {
            s = ap_[sp][st].Process(s, kApG[st]);
        }
        // Last stage is modulated for organic pitch wobble
        const float lfo_val   = spring_lfo_[sp].Process();
        const float mod_delay = static_cast<float>(ap_delays[5]) + lfo_val * mod_depth_;
        s = ap_[sp][5].ProcessMod(s, kApG[5], mod_delay);
        const float c = comb_[sp].Process(s) * comb_makeup_[sp];
        // Alternate L/R per spring
        if (sp == 0)      { out_l += c; out_r += c * 0.7f; }
        else if (sp == 1) { out_l += c * 0.6f; out_r += c; }
        else              { out_l += c * 0.8f; out_r += c * 0.8f; }
    }

    const float scale = 1.0f / static_cast<float>(n_springs);
    return StereoFrame{
        tone_[0].Process(out_l * scale),
        tone_[1].Process(out_r * scale)
    };
}

void SpringReverb::SetHold(bool h) {
    hold_ = h;
    if (h) {
        for (auto& c : comb_) c.SetFeedback(1.0f);
    } else {
        for (int sp = 0; sp < 3; ++sp) comb_[sp].SetFeedback(comb_fb_[sp]);
    }
}

} // namespace pedal
