#include "room_reverb.h"
#include "../config/constants.h"
#include <cmath>

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

// Mirror the right-input reflection field so left-only and right-only sources
// retain a coherent, complementary spatial image.
static constexpr ErTap kErTapsMirrored[] = {
    {175,  0.80f,  0.70f},
    {307,  0.70f, -0.70f},
    {467,  0.60f,  0.50f},
    {683,  0.50f, -0.50f},
    {905,  0.40f,  0.85f},
    {1109, 0.35f, -0.85f},
    {1439, 0.28f,  0.30f},
    {1723, 0.22f, -0.30f},
};

} // namespace

void RoomReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    er_l_.Init(buf_er_l_, 4096);
    er_r_.Init(buf_er_r_, 4096);
    er_l_.SetTaps(kErTaps, 8);
    er_r_.SetTaps(kErTapsMirrored, 8);

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

    Fdn::Config fdn_cfg{};
    fdn_cfg.n_lines     = 8;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;  fdn_cfg.delays[0] = 954;
    fdn_cfg.bufs[1]     = buf_fdn4_;  fdn_cfg.delays[1] = 1109;
    fdn_cfg.bufs[2]     = buf_fdn1_;  fdn_cfg.delays[2] = 1297;
    fdn_cfg.bufs[3]     = buf_fdn5_;  fdn_cfg.delays[3] = 1489;
    fdn_cfg.bufs[4]     = buf_fdn2_;  fdn_cfg.delays[4] = 1849;
    fdn_cfg.bufs[5]     = buf_fdn6_;  fdn_cfg.delays[5] = 2063;
    fdn_cfg.bufs[6]     = buf_fdn3_;  fdn_cfg.delays[6] = 2400;
    fdn_cfg.bufs[7]     = buf_fdn7_;  fdn_cfg.delays[7] = 2731;
    const size_t fdn_sizes[8] = {1907, 2219, 2593, 2979, 3697, 4127, 4799, 5463};
    for (int i = 0; i < 8; ++i) fdn_cfg.buffer_sizes[i] = fdn_sizes[i];
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(2.0f);
    fdn_.SetDamping(0.3f);
}

void RoomReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    er_l_.Reset();
    er_r_.Reset();
    diffuser_l_.Reset();
    diffuser_r_.Reset();
    fdn_.Reset();
    early_mix_ = 0.4f;
}

void RoomReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    const float rounded = (delay_samples < 1.0f ? 1.0f : delay_samples) + 0.5f;
    pre_delay_l_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    pre_delay_r_.SetDelay(static_cast<float>(static_cast<size_t>(rounded)));
    const float size_scale = 0.65f + params.param1 * 0.70f;
    fdn_.SetSize(size_scale);
    const float calibrated_decay = params.decay * 1.34f;
    fdn_.SetDecay(calibrated_decay);
    fdn_.SetDampFromRt60Ratio(calibrated_decay, 0.30f + params.tone * 0.70f);
    fdn_.SetModulation(params.mod * 8.0f);
    diffuser_l_.SetDiffusion(params.param2);
    diffuser_r_.SetDiffusion(params.param2);
    // Larger rooms are perceived as having a more dominant late field. This
    // keeps the current delay topology stable while making Size musically
    // useful and preserving the former 40/60 balance at the centre setting.
    early_mix_ = 0.60f - params.param1 * 0.40f;

    ErTap taps_l[8];
    ErTap taps_r[8];
    for (int i = 0; i < 8; ++i) {
        taps_l[i] = kErTaps[i];
        taps_r[i] = kErTapsMirrored[i];
        taps_l[i].delay_samples = static_cast<uint16_t>(std::lround(kErTaps[i].delay_samples * size_scale));
        taps_r[i].delay_samples = static_cast<uint16_t>(std::lround(kErTapsMirrored[i].delay_samples * size_scale));
    }
    er_l_.SetTaps(taps_l, 8);
    er_r_.SetTaps(taps_r, 8);
    fdn_.PrepareBlock();
}

StereoFrame RoomReverb::Process(float input, const ParamSet& /*params*/) {
    return Process(StereoFrame{input, input}, ParamSet{});
}

StereoFrame RoomReverb::Process(StereoFrame input, const ParamSet& /*params*/) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const StereoFrame er_l = er_l_.Process(pre_delay_l_.Read());
    const StereoFrame er_r = er_r_.Process(pre_delay_r_.Read());

    // Cross-couple the reflection fields just enough to retain room width
    // while preserving distinct left/right excitation into the FDN.
    const StereoFrame er{
        0.5f * (er_l.left + er_r.left),
        0.5f * (er_l.right + er_r.right)
    };
    const StereoFrame diffused{
        diffuser_l_.Process(0.5f * (er_l.left + er_r.right)),
        diffuser_r_.Process(0.5f * (er_l.right + er_r.left))
    };
    const StereoFrame late = fdn_.Process(diffused);

    const StereoFrame out{
        er.left  * early_mix_ + late.left  * (1.0f - early_mix_),
        er.right * early_mix_ + late.right * (1.0f - early_mix_)
    };
    return out;
}

} // namespace pedal
