#include "cloud_reverb.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

void CloudReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    float* d0_bufs_l[Diffuser::STAGES] = {
        buf_d0_l0_, buf_d0_l1_, buf_d0_l2_, buf_d0_l3_
    };
    float* d0_bufs_r[Diffuser::STAGES] = {
        buf_d0_r0_, buf_d0_r1_, buf_d0_r2_, buf_d0_r3_
    };
    const size_t d0_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser0_l_.Init(d0_bufs_l, d0_sizes);
    diffuser0_r_.Init(d0_bufs_r, d0_sizes);
    diffuser0_l_.SetDiffusion(0.7f);
    diffuser0_r_.SetDiffusion(0.7f);

    float* d1_bufs_l[Diffuser::STAGES] = {
        buf_d1_l0_, buf_d1_l1_, buf_d1_l2_, buf_d1_l3_
    };
    float* d1_bufs_r[Diffuser::STAGES] = {
        buf_d1_r0_, buf_d1_r1_, buf_d1_r2_, buf_d1_r3_
    };
    const size_t d1_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser1_l_.Init(d1_bufs_l, d1_sizes);
    diffuser1_r_.Init(d1_bufs_r, d1_sizes);
    diffuser1_l_.SetDiffusion(0.7f);
    diffuser1_r_.SetDiffusion(0.7f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;   fdn_cfg.delays[0] = 2401;
    fdn_cfg.bufs[1]     = buf_fdn1_;   fdn_cfg.delays[1] = 3076;
    fdn_cfg.bufs[2]     = buf_fdn2_;   fdn_cfg.delays[2] = 3850;
    fdn_cfg.bufs[3]     = buf_fdn3_;   fdn_cfg.delays[3] = 4501;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(10.0f);
    fdn_.SetDamping(0.3f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
}

void CloudReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    diffuser0_l_.Reset();
    diffuser0_r_.Reset();
    diffuser1_l_.Reset();
    diffuser1_r_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
}

void CloudReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetModulation(params.mod * Fdn::MAX_MOD_DEPTH_SAMPLES);

    const float diff = 0.5f + params.param1 * 0.35f;
    diffuser0_l_.SetDiffusion(diff);
    diffuser0_r_.SetDiffusion(diff);
    diffuser1_l_.SetDiffusion(diff);
    diffuser1_r_.SetDiffusion(diff);

    const float damp = 0.5f - params.param2 * 0.4f;
    fdn_.SetDamping(damp);

    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);
    fdn_.PrepareBlock();
}

StereoFrame CloudReverb::Process(float input, const ParamSet& /*params*/) {
    return Process(StereoFrame{input, input}, ParamSet{});
}

StereoFrame CloudReverb::Process(StereoFrame input, const ParamSet& /*params*/) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    float diffused_l = diffuser0_l_.Process(pre_delay_l_.Read());
    float diffused_r = diffuser0_r_.Process(pre_delay_r_.Read());
    diffused_l = diffuser1_l_.Process(diffused_l);
    diffused_r = diffuser1_r_.Process(diffused_r);
    const StereoFrame late = fdn_.Process({diffused_l, diffused_r});

    const StereoFrame out{
        tone_[0].Process(late.left),
        tone_[1].Process(late.right)
    };
    return out;
}

} // namespace pedal
