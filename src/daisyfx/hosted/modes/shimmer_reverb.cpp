#include "shimmer_reverb.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

void ShimmerReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    float* diff_bufs_l[Diffuser::STAGES] = {
        buf_diff_l0_, buf_diff_l1_, buf_diff_l2_, buf_diff_l3_
    };
    float* diff_bufs_r[Diffuser::STAGES] = {
        buf_diff_r0_, buf_diff_r1_, buf_diff_r2_, buf_diff_r3_
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_l_.Init(diff_bufs_l, diff_sizes);
    diffuser_r_.Init(diff_bufs_r, diff_sizes);
    diffuser_l_.SetDiffusion(0.65f);
    diffuser_r_.SetDiffusion(0.65f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;
    fdn_cfg.bufs[1]     = buf_fdn1_;
    fdn_cfg.bufs[2]     = buf_fdn2_;
    fdn_cfg.bufs[3]     = buf_fdn3_;
    fdn_cfg.delays[0]   = 1365;
    fdn_cfg.delays[1]   = 1626;
    fdn_cfg.delays[2]   = 1932;
    fdn_cfg.delays[3]   = 2254;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(3.0f);
    fdn_.SetDamping(0.2f);

    pitch_shifter_[0].Init(buf_pitch0_, 8192, REVERB_SAMPLE_RATE);
    pitch_shifter_[1].Init(buf_pitch1_, 8192, REVERB_SAMPLE_RATE);
    pitch_shifter_[0].SetShift(12.0f);
    pitch_shifter_[1].SetShift(7.0f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    hold_           = false;
    pitch_fb_l_     = 0.0f;
    pitch_fb_r_     = 0.0f;
}

void ShimmerReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    diffuser_l_.Reset();
    diffuser_r_.Reset();
    fdn_.Reset();
    pitch_shifter_[0].Reset();
    pitch_shifter_[1].Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    hold_           = false;
    pitch_fb_l_     = 0.0f;
    pitch_fb_r_     = 0.0f;
}

void ShimmerReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * Fdn::MAX_MOD_DEPTH_SAMPLES);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    // Stagger Left and Right pitch shifts by +/-10 cents (0.10 semitones) for stereo width
    pitch_shifter_[0].SetShift(params.param1 - 0.10f);
    pitch_shifter_[1].SetShift(params.param2 + 0.10f);
    fdn_.PrepareBlock();
}

StereoFrame ShimmerReverb::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame ShimmerReverb::Process(StereoFrame input, const ParamSet& params) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const StereoFrame diffused{
        diffuser_l_.Process(pre_delay_l_.Read()),
        diffuser_r_.Process(pre_delay_r_.Read())
    };

    // Combine dry diffused input with previous shimmer feedback in stereo
    const float shimmer_amount = hold_ ? 0.0f : params.mod;
    StereoFrame fdn_in;
    fdn_in.left  = diffused.left + shimmer_amount * 0.5f * pitch_fb_l_;
    fdn_in.right = diffused.right + shimmer_amount * 0.5f * pitch_fb_r_;

    const StereoFrame late = fdn_.Process(fdn_in);

    // Pitch-shift FDN output for the next sample's feedback.
    // Feed Left channel output to shifter 0, and Right channel output to shifter 1.
    pitch_fb_l_ = pitch_shifter_[0].Process(late.left);
    pitch_fb_r_ = pitch_shifter_[1].Process(late.right);

    // Blend pitch-shifted outputs directly into the left and right output channels
    const StereoFrame out{
        tone_[0].Process(late.left  + shimmer_amount * pitch_fb_l_ * 0.5f),
        tone_[1].Process(late.right + shimmer_amount * pitch_fb_r_ * 0.5f)
    };
    return out;
}

void ShimmerReverb::SetHold(bool h) {
    hold_ = h;
    fdn_.SetHold(h);
}

} // namespace pedal
