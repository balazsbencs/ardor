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

} // namespace

void HallReverb::Init() {
    pre_delay_.Init(buf_pre_delay_, 24000);
    pre_delay_.SetDelay(1.0f);

    er_.Init(buf_er_, 4096);
    er_.SetTaps(kErTaps, 4);

    float* diff_bufs[Diffuser::STAGES] = {
        buf_diff0_, buf_diff1_, buf_diff2_, buf_diff3_
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_.Init(diff_bufs, diff_sizes);
    diffuser_.SetDiffusion(0.65f);

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
    pre_delay_.Reset();
    er_.Reset();
    diffuser_.Reset();
    fdn_.Reset();
}

void HallReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    // Round to integer samples: pre-delay has no sub-sample modulation so Hermite
    // precision is wasted. Integer delay triggers the Read() fast path (1 read vs 4).
    const float rounded = (delay_samples < 1.0f ? 1.0f : delay_samples) + 0.5f;
    pre_delay_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    fdn_.SetDecay(params.decay);
    // tone: 0=dark (HF RT60 = 30% of LF), 1=bright (HF RT60 = LF, uniform decay)
    fdn_.SetDampFromRt60Ratio(params.decay, 0.30f + params.tone * 0.70f);
    fdn_.SetModulation(params.mod * 8.0f);
    // Param1 controls pre-diffusion density (0 = minimal, 1 = maximum)
    diffuser_.SetDiffusion(0.35f + params.param1 * 0.45f);
    fdn_.PrepareBlock();
}

StereoFrame HallReverb::Process(float input, const ParamSet& /*params*/) {
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    const StereoFrame er = er_.Process(pre);

    // Weighted asymmetric sum preserves spatial impression from ER stage
    // rather than discarding it with a symmetric 50/50 collapse.
    const float diffused = diffuser_.Process(0.65f * er.left + 0.35f * er.right);

    const StereoFrame late = fdn_.Process(diffused);

    const StereoFrame out{
        er.left  * 0.35f + late.left  * 0.65f,
        er.right * 0.35f + late.right * 0.65f
    };
    return out;
}

} // namespace pedal
