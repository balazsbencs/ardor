#include "hall_reverb.h"
#include "../config/constants.h"
#include <cmath>

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
    { 251,  0.78f,  0.70f},
    { 439,  0.68f, -0.70f},
    {1373,  0.38f,  0.90f},
    {1721,  0.32f, -0.90f},
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
    const size_t diff_sizes_l[Diffuser::STAGES] = {
        sizeof(buf_diff_l0_) / sizeof(float), sizeof(buf_diff_l1_) / sizeof(float),
        sizeof(buf_diff_l2_) / sizeof(float), sizeof(buf_diff_l3_) / sizeof(float) };
    const size_t diff_sizes_r[Diffuser::STAGES] = {
        sizeof(buf_diff_r0_) / sizeof(float), sizeof(buf_diff_r1_) / sizeof(float),
        sizeof(buf_diff_r2_) / sizeof(float), sizeof(buf_diff_r3_) / sizeof(float) };
    diffuser_l_.Init(diff_bufs_l, diff_sizes_l, diffuser_delays::HALL_L);
    diffuser_r_.Init(diff_bufs_r, diff_sizes_r, diffuser_delays::HALL_R);
    diffuser_l_.SetDiffusion(0.65f);
    diffuser_r_.SetDiffusion(0.65f);


    // Slow drift on the long allpass stages breaks up the fixed
    // ringing a static cascade produces. Well below chorus depth.
    diffuser_l_.SetModulationRate(0.0900f, REVERB_SAMPLE_RATE);
    diffuser_l_.SetModulation(2.5f);
    diffuser_r_.SetModulationRate(0.1053f, REVERB_SAMPLE_RATE);
    diffuser_r_.SetModulation(2.5f);

    Fdn::Config fdn_cfg{};
    fdn_cfg.n_lines     = 8;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;  fdn_cfg.delays[0] = 1654;
    fdn_cfg.bufs[1]     = buf_fdn4_;  fdn_cfg.delays[1] = 1831;
    fdn_cfg.bufs[2]     = buf_fdn1_;  fdn_cfg.delays[2] = 2080;
    fdn_cfg.bufs[3]     = buf_fdn5_;  fdn_cfg.delays[3] = 2393;
    fdn_cfg.bufs[4]     = buf_fdn2_;  fdn_cfg.delays[4] = 2952;
    fdn_cfg.bufs[5]     = buf_fdn6_;  fdn_cfg.delays[5] = 3221;
    fdn_cfg.bufs[6]     = buf_fdn3_;  fdn_cfg.delays[6] = 3499;
    fdn_cfg.bufs[7]     = buf_fdn7_;  fdn_cfg.delays[7] = 3907;
    const size_t fdn_sizes[8] = {3307, 3663, 4159, 4787, 5903, 6443, 6997, 7815};
    for (int i = 0; i < 8; ++i) fdn_cfg.buffer_sizes[i] = fdn_sizes[i];
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
    mid_fast_[0] = mid_fast_[1] = 0.0f;
    mid_slow_[0] = mid_slow_[1] = 0.0f;
    mid_scale_ = 0.0f;
}

void HallReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    // Round to integer samples: pre-delay has no sub-sample modulation so Hermite
    // precision is wasted. Integer delay triggers the Read() fast path (1 read vs 4).
    const float rounded = (delay_samples < 1.0f ? 1.0f : delay_samples) + 0.5f;
    pre_delay_l_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    pre_delay_r_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    const float calibrated_decay = params.decay * 1.14f;
    fdn_.SetDecay(calibrated_decay);
    // tone: 0=dark (HF RT60 = 30% of LF), 1=bright (HF RT60 = LF, uniform decay)
    fdn_.SetDampFromRt60Ratio(calibrated_decay, 0.30f + params.tone * 0.70f);
    fdn_.SetModulation(params.mod * 8.0f);
    // Param1 controls pre-diffusion density (0 = minimal, 1 = maximum)
    diffuser_l_.SetDiffusion(0.35f + params.param1 * 0.45f);
    diffuser_r_.SetDiffusion(0.35f + params.param1 * 0.45f);
    // Broad post-tank mid control, approximately +/-6 dB. Two inexpensive
    // one-poles form a 250 Hz--2.5 kHz band without putting another resonant
    // filter inside the feedback loop.
    const float mid_db = (params.param2 - 0.5f) * 12.0f;
    mid_scale_ = std::pow(10.0f, mid_db / 20.0f) - 1.0f;
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

    const float tank[2] = {
        er.left  * 0.35f + late.left  * 0.65f,
        er.right * 0.35f + late.right * 0.65f
    };
    StereoFrame out{};
    float* channels[2] = {&out.left, &out.right};
    for (int ch = 0; ch < 2; ++ch) {
        mid_fast_[ch] += 0.48f * (tank[ch] - mid_fast_[ch]);
        mid_slow_[ch] += 0.063f * (tank[ch] - mid_slow_[ch]);
        *channels[ch] = tank[ch] + mid_scale_ * (mid_fast_[ch] - mid_slow_[ch]);
    }
    return out;
}

} // namespace pedal
