#include "bloom_reverb.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

void BloomReverb::Init() {
    pre_delay_.Init(buf_pre_delay_, 24000);
    pre_delay_.SetDelay(1.0f);

    float* diff_bufs[Diffuser::STAGES] = {
        buf_diff0_, buf_diff1_, buf_diff2_, buf_diff3_
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_.Init(diff_bufs, diff_sizes);
    diffuser_.SetDiffusion(0.65f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;
    fdn_cfg.bufs[1]     = buf_fdn1_;
    fdn_cfg.bufs[2]     = buf_fdn2_;
    fdn_cfg.bufs[3]     = buf_fdn3_;
    fdn_cfg.delays[0]   = 1452;
    fdn_cfg.delays[1]   = 1746;
    fdn_cfg.delays[2]   = 2080;
    fdn_cfg.delays[3]   = 2407;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(3.0f);
    fdn_.SetDamping(0.25f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    input_env_.Init(2.0f, 160.0f);
    bloom_env_       = 0.0f;
    input_env_slow_  = 0.0f;
    bloom_rate_      = 1.0f / (2.0f * REVERB_SAMPLE_RATE);
    bloom_feedback_  = 0.0f;
    bloom_fb_signal_ = 0.0f;
}

void BloomReverb::Reset() {
    pre_delay_.Reset();
    diffuser_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    input_env_.Init(2.0f, 160.0f);
    bloom_env_       = 0.0f;
    input_env_slow_  = 0.0f;
    bloom_fb_signal_ = 0.0f;
}

void BloomReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * Fdn::MAX_MOD_DEPTH_SAMPLES);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    const float bloom_time_s = 0.5f + params.param1 * 4.5f;
    bloom_rate_    = 1.0f / (bloom_time_s * REVERB_SAMPLE_RATE);
    bloom_feedback_ = params.param2 * 0.7f;
    fdn_.PrepareBlock();
}

StereoFrame BloomReverb::Process(float input, const ParamSet& /*params*/) {
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    const float diffused = diffuser_.Process(pre);

    const float input_env = input_env_.Process(pre);
    const bool onset = input_env > 0.035f && input_env > input_env_slow_ + 0.025f;
    input_env_slow_ += 0.0015f * (input_env - input_env_slow_);
    if (onset) bloom_env_ = 0.0f;

    // Bloom envelope rises slowly from each detected onset toward 1.
    bloom_env_ += bloom_rate_ * (1.0f - bloom_env_);

    // FDN input: diffused signal + bloom-gated feedback from previous output
    const float fdn_in = diffused + bloom_feedback_ * bloom_fb_signal_;
    const StereoFrame late = fdn_.Process(fdn_in);

    // Store mono output scaled by bloom envelope for next sample's feedback
    bloom_fb_signal_ = bloom_env_ * 0.5f * (late.left + late.right);

    const StereoFrame out{
        tone_[0].Process(late.left  * bloom_env_),
        tone_[1].Process(late.right * bloom_env_)
    };
    return out;
}

} // namespace pedal
