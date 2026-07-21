#include "hall_reverb.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

static constexpr ErTap kErTaps[4] = {
    { 240,  0.78f, -0.70f},
    { 456,  0.68f,  0.70f},
    {1344,  0.38f, -0.90f},
    {1752,  0.32f,  0.90f},
};

static constexpr ErTap kErTapsMirrored[4] = {
    { 240,  0.78f,  0.70f},
    { 456,  0.68f, -0.70f},
    {1344,  0.38f,  0.90f},
    {1752,  0.32f, -0.90f},
};

} // namespace

void HallReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    er_l_.Init(buf_er_l_, 4096);
    er_r_.Init(buf_er_r_, 4096);
    er_l_.SetTaps(kErTaps, 4);
    er_r_.SetTaps(kErTapsMirrored, 4);

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
    fdn_cfg.bufs[0]     = buf_fdn0_;  fdn_cfg.delays[0] = 1654;
    fdn_cfg.bufs[1]     = buf_fdn1_;  fdn_cfg.delays[1] = 2080;
    fdn_cfg.bufs[2]     = buf_fdn2_;  fdn_cfg.delays[2] = 2952;
    fdn_cfg.bufs[3]     = buf_fdn3_;  fdn_cfg.delays[3] = 3499;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(3.0f);
    fdn_.SetDamping(0.25f);
}

void HallReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    er_l_.Reset();
    er_r_.Reset();
    diffuser_l_.Reset();
    diffuser_r_.Reset();
    fdn_.Reset();
}

void HallReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    // Round to integer samples: pre-delay has no sub-sample modulation so Hermite
    // precision is wasted. Integer delay triggers the Read() fast path (1 read vs 4).
    const float rounded = (delay_samples < 1.0f ? 1.0f : delay_samples) + 0.5f;
    pre_delay_l_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    pre_delay_r_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    fdn_.SetDecay(params.decay);
    // tone: 0=dark (HF RT60 = 30% of LF), 1=bright (HF RT60 = LF, uniform decay)
    fdn_.SetDampFromRt60Ratio(params.decay, 0.30f + params.tone * 0.70f);
    fdn_.SetModulation(params.mod * 8.0f);
    // Param1 controls pre-diffusion density (0 = minimal, 1 = maximum)
    diffuser_l_.SetDiffusion(0.35f + params.param1 * 0.45f);
    diffuser_r_.SetDiffusion(0.35f + params.param1 * 0.45f);
    fdn_.PrepareBlock();
}

StereoFrame HallReverb::Process(float input, const ParamSet& /*params*/) {
    return Process(StereoFrame{input, input}, ParamSet{});
}

StereoFrame HallReverb::Process(StereoFrame input, const ParamSet& /*params*/) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const StereoFrame er_l = er_l_.Process(pre_delay_l_.Read());
    const StereoFrame er_r = er_r_.Process(pre_delay_r_.Read());
    const StereoFrame er{
        0.5f * (er_l.left + er_r.left),
        0.5f * (er_l.right + er_r.right)
    };
    const StereoFrame diffused{
        diffuser_l_.Process(0.65f * er_l.left + 0.35f * er_r.right),
        diffuser_r_.Process(0.65f * er_l.right + 0.35f * er_r.left)
    };
    const StereoFrame late = fdn_.Process(diffused);

    const StereoFrame out{
        er.left  * 0.35f + late.left  * 0.65f,
        er.right * 0.35f + late.right * 0.65f
    };
    return out;
}

} // namespace pedal
