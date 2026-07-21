#include "bloom_reverb.h"
#include "../config/constants.h"

#include <algorithm>

using namespace pedal::reverb_fx;

namespace pedal {

void BloomReverb::Init() {
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
    input_env_.Init(2.0f, 160.0f, REVERB_SAMPLE_RATE);
    bloom_env_       = 0.0f;
    input_env_slow_  = 0.0f;
    bloom_rate_      = 1.0f / (2.0f * REVERB_SAMPLE_RATE);
    bloom_feedback_  = 0.0f;
    bloom_fb_signal_ = 0.0f;
}

void BloomReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    diffuser_l_.Reset();
    diffuser_r_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    input_env_.Init(2.0f, 160.0f, REVERB_SAMPLE_RATE);
    bloom_env_       = 0.0f;
    input_env_slow_  = 0.0f;
    bloom_fb_signal_ = 0.0f;
}

void BloomReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * Fdn::MAX_MOD_DEPTH_SAMPLES);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    // ParamSet contains physical values after the adapter's one mapping pass.
    const float bloom_time_s = std::clamp(params.param1, 0.5f, 5.0f);
    bloom_rate_    = 1.0f / (bloom_time_s * REVERB_SAMPLE_RATE);
    bloom_feedback_ = std::clamp(params.param2, 0.0f, 0.7f);
    fdn_.PrepareBlock();
}

StereoFrame BloomReverb::Process(float input, const ParamSet& /*params*/) {
    return Process(StereoFrame{input, input}, ParamSet{});
}

StereoFrame BloomReverb::Process(StereoFrame input, const ParamSet& /*params*/) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const float pre_l = pre_delay_l_.Read();
    const float pre_r = pre_delay_r_.Read();
    const StereoFrame diffused{
        diffuser_l_.Process(pre_l),
        diffuser_r_.Process(pre_r)
    };

    const float input_env = input_env_.Process(0.5f * (pre_l + pre_r));
    const bool onset = input_env > 0.035f && input_env > input_env_slow_ + 0.025f;
    input_env_slow_ += 0.0015f * (input_env - input_env_slow_);
    if (onset) bloom_env_ = 0.0f;

    // Bloom envelope rises slowly from each detected onset toward 1.
    bloom_env_ += bloom_rate_ * (1.0f - bloom_env_);

    // FDN input: diffused signal + bloom-gated feedback from previous output
    const StereoFrame fdn_in{
        diffused.left + bloom_feedback_ * bloom_fb_signal_,
        diffused.right + bloom_feedback_ * bloom_fb_signal_
    };
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
