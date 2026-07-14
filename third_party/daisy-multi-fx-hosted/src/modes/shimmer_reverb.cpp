#include "shimmer_reverb.h"
#include "daisy_seed.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

static float DSY_SDRAM_BSS buf_pre_delay[24000];
static float DSY_SDRAM_BSS buf_diff0[Diffuser::kDelays[0] + 1];
static float DSY_SDRAM_BSS buf_diff1[Diffuser::kDelays[1] + 1];
static float DSY_SDRAM_BSS buf_diff2[Diffuser::kDelays[2] + 1];
static float DSY_SDRAM_BSS buf_diff3[Diffuser::kDelays[3] + 1];
static float DSY_SDRAM_BSS buf_fdn0[2730];
static float DSY_SDRAM_BSS buf_fdn1[3252];
static float DSY_SDRAM_BSS buf_fdn2[3864];
static float DSY_SDRAM_BSS buf_fdn3[4508];
static float DSY_SDRAM_BSS buf_pitch0[8192];
static float DSY_SDRAM_BSS buf_pitch1[8192];

} // namespace

void ShimmerReverb::Init() {
    pre_delay_.Init(buf_pre_delay, 24000);
    pre_delay_.SetDelay(1.0f);

    float* diff_bufs[Diffuser::STAGES] = {
        buf_diff0, buf_diff1, buf_diff2, buf_diff3
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_.Init(diff_bufs, diff_sizes);
    diffuser_.SetDiffusion(0.65f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0;
    fdn_cfg.bufs[1]     = buf_fdn1;
    fdn_cfg.bufs[2]     = buf_fdn2;
    fdn_cfg.bufs[3]     = buf_fdn3;
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

    pitch_shifter_[0].Init(buf_pitch0, 8192, REVERB_SAMPLE_RATE);
    pitch_shifter_[1].Init(buf_pitch1, 8192, REVERB_SAMPLE_RATE);
    pitch_shifter_[0].SetShift(12.0f);
    pitch_shifter_[1].SetShift(7.0f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    hold_           = false;
    pitch_fb_l_     = 0.0f;
    pitch_fb_r_     = 0.0f;
}

void ShimmerReverb::Reset() {
    pre_delay_.Reset();
    diffuser_.Reset();
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
    pre_delay_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
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
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    const float diffused = diffuser_.Process(pre);

    // Combine dry diffused input with previous shimmer feedback in stereo
    const float shimmer_amount = hold_ ? 0.0f : params.mod;
    StereoFrame fdn_in;
    fdn_in.left  = diffused + shimmer_amount * 0.5f * pitch_fb_l_;
    fdn_in.right = diffused + shimmer_amount * 0.5f * pitch_fb_r_;

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
