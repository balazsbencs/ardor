#include "room_reverb.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

// ER tap table: 8 taps, typical small-room reflections
static constexpr ErTap kErTaps[] = {
    {168,  0.80f, -0.70f},
    {312,  0.70f,  0.70f},
    {456,  0.60f, -0.50f},
    {696,  0.50f,  0.50f},
    {888,  0.40f, -0.85f},
    {1128, 0.35f,  0.85f},
    {1416, 0.28f, -0.30f},
    {1752, 0.22f,  0.30f},
};

} // namespace

void RoomReverb::Init() {
    pre_delay_.Init(buf_pre_delay_, 24000);
    pre_delay_.SetDelay(1.0f);

    er_.Init(buf_er_, 4096);
    er_.SetTaps(kErTaps, 8);

    float* diff_bufs[Diffuser::STAGES] = {
        buf_diff0_, buf_diff1_, buf_diff2_, buf_diff3_
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_.Init(diff_bufs, diff_sizes);
    diffuser_.SetDiffusion(0.65f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;  fdn_cfg.delays[0] = 954;
    fdn_cfg.bufs[1]     = buf_fdn1_;  fdn_cfg.delays[1] = 1297;
    fdn_cfg.bufs[2]     = buf_fdn2_;  fdn_cfg.delays[2] = 1849;
    fdn_cfg.bufs[3]     = buf_fdn3_;  fdn_cfg.delays[3] = 2400;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(2.0f);
    fdn_.SetDamping(0.3f);
}

void RoomReverb::Reset() {
    pre_delay_.Reset();
    er_.Reset();
    diffuser_.Reset();
    fdn_.Reset();
}

void RoomReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    const float rounded = (delay_samples < 1.0f ? 1.0f : delay_samples) + 0.5f;
    pre_delay_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    fdn_.SetDecay(params.decay);
    fdn_.SetDampFromRt60Ratio(params.decay, 0.30f + params.tone * 0.70f);
    fdn_.SetModulation(params.mod * 8.0f);
    diffuser_.SetDiffusion(params.param2);
    fdn_.PrepareBlock();
}

StereoFrame RoomReverb::Process(float input, const ParamSet& /*params*/) {
    // Pre-delay
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    // Early reflections
    const StereoFrame er = er_.Process(pre);

    // Diffuse and enter FDN
    const float diffused = diffuser_.Process(0.5f * (er.left + er.right));

    // FDN late reverb
    const StereoFrame late = fdn_.Process(diffused);

    // Tone shaping
    const StereoFrame out{
        er.left  * 0.4f + late.left  * 0.6f,
        er.right * 0.4f + late.right * 0.6f
    };
    return out;
}

} // namespace pedal
