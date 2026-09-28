#include "bloom_reverb.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

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
    const size_t diff_sizes_l[Diffuser::STAGES] = {
        sizeof(buf_diff_l0_) / sizeof(float), sizeof(buf_diff_l1_) / sizeof(float),
        sizeof(buf_diff_l2_) / sizeof(float), sizeof(buf_diff_l3_) / sizeof(float) };
    const size_t diff_sizes_r[Diffuser::STAGES] = {
        sizeof(buf_diff_r0_) / sizeof(float), sizeof(buf_diff_r1_) / sizeof(float),
        sizeof(buf_diff_r2_) / sizeof(float), sizeof(buf_diff_r3_) / sizeof(float) };
    diffuser_l_.Init(diff_bufs_l, diff_sizes_l, diffuser_delays::BLOOM_L);
    diffuser_r_.Init(diff_bufs_r, diff_sizes_r, diffuser_delays::BLOOM_R);
    diffuser_l_.SetDiffusion(0.65f);
    diffuser_r_.SetDiffusion(0.65f);


    // Slow drift on the long allpass stages breaks up the fixed
    // ringing a static cascade produces. Well below chorus depth.
    diffuser_l_.SetModulationRate(0.0700f, REVERB_SAMPLE_RATE);
    diffuser_l_.SetModulation(2.5f);
    diffuser_r_.SetModulationRate(0.0819f, REVERB_SAMPLE_RATE);
    diffuser_r_.SetModulation(2.5f);

    Fdn::Config fdn_cfg{};
    fdn_cfg.n_lines     = 8;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;
    fdn_cfg.bufs[1]     = buf_fdn1_;
    fdn_cfg.bufs[2]     = buf_fdn2_;
    fdn_cfg.bufs[3]     = buf_fdn3_;
    fdn_cfg.delays[0]   = 1452;
    fdn_cfg.delays[1]   = 1746;
    fdn_cfg.delays[2]   = 2080;
    fdn_cfg.delays[3]   = 2407;
    fdn_cfg.bufs[4]     = buf_fdn4_; fdn_cfg.delays[4] = 1583;
    fdn_cfg.bufs[5]     = buf_fdn5_; fdn_cfg.delays[5] = 1901;
    fdn_cfg.bufs[6]     = buf_fdn6_; fdn_cfg.delays[6] = 2243;
    fdn_cfg.bufs[7]     = buf_fdn7_; fdn_cfg.delays[7] = 2593;
    const size_t fdn_sizes[8] = {2904, 3492, 4160, 4814, 3166, 3802, 4486, 5186};
    for (int i = 0; i < 8; ++i) fdn_cfg.buffer_sizes[i] = fdn_sizes[i];
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(3.0f);
    fdn_.SetDamping(0.25f);

    tone_[0].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    tone_[1].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    input_env_.Init(2.0f, 160.0f, REVERB_SAMPLE_RATE);
    bloom_env_       = 0.0f;
    input_env_slow_  = 0.0f;
    onset_active_ = false;
    bloom_rate_      = 1.0f / (2.0f * REVERB_SAMPLE_RATE);
}

void BloomReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    diffuser_l_.Reset();
    diffuser_r_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    tone_[1].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    input_env_.Init(2.0f, 160.0f, REVERB_SAMPLE_RATE);
    bloom_env_       = 0.0f;
    input_env_slow_  = 0.0f;
    onset_active_ = false;
}

void BloomReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    // Feedback increases the tank's own loop gain by extending its RT60.
    // Unlike an output-to-input return, this stays below unity at every
    // setting and makes the control increase sustain as expected.
    const float feedback = std::clamp(params.param2, 0.0f, 0.7f);
    const float tank_decay = params.decay * (1.0f + feedback);
    fdn_.SetDecay(tank_decay);
    fdn_.SetDampFromRt60Ratio(tank_decay, 0.25f + params.tone * 0.75f);
    fdn_.SetModulation(params.mod * Fdn::MAX_MOD_DEPTH_SAMPLES);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    // ParamSet contains physical values after the adapter's one mapping pass.
    const float bloom_time_s = std::clamp(params.param1, 0.5f, 5.0f);
    bloom_rate_    = 1.0f / (bloom_time_s * REVERB_SAMPLE_RATE);
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

    const float input_env = input_env_.Process(std::max(std::fabs(pre_l), std::fabs(pre_r)));
    const bool onset = input_env > 0.001f
        && input_env > input_env_slow_ * 1.5f + 0.0003f;
    input_env_slow_ += 0.0015f * (input_env - input_env_slow_);
    if (onset && !onset_active_) bloom_env_ = 0.0f;
    onset_active_ = onset;

    // Bloom envelope rises slowly from each detected onset toward 1.
    bloom_env_ += bloom_rate_ * (1.0f - bloom_env_);

    const StereoFrame late = fdn_.Process(diffused);

    const StereoFrame out{
        tone_[0].Process(late.left  * bloom_env_),
        tone_[1].Process(late.right * bloom_env_)
    };
    return out;
}

} // namespace pedal
