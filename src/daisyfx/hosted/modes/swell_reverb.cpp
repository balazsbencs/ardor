#include "swell_reverb.h"
#include "../config/constants.h"

#include <algorithm>

using namespace pedal::reverb_fx;

namespace pedal {

void SwellReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;
    fdn_cfg.bufs[1]     = buf_fdn1_;
    fdn_cfg.bufs[2]     = buf_fdn2_;
    fdn_cfg.bufs[3]     = buf_fdn3_;
    fdn_cfg.delays[0]   = 1261;
    fdn_cfg.delays[1]   = 1540;
    fdn_cfg.delays[2]   = 1830;
    fdn_cfg.delays[3]   = 2116;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(2.0f);
    fdn_.SetDamping(0.3f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    env_follow_.Init(5.0f, 100.0f, REVERB_SAMPLE_RATE);

    ramp_gain_ = 0.0f;
    swell_dry_ = true;
    ramp_rate_ = 1.0f / (0.5f * REVERB_SAMPLE_RATE);
}

void SwellReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    env_follow_.Init(5.0f, 100.0f, REVERB_SAMPLE_RATE);
    ramp_gain_ = 0.0f;
    swell_dry_ = true;
}

void SwellReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * 4.0f);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    // ParamSet contains the physical 80 ms–4 s rise time.
    const float rise_time_s = std::clamp(params.param1, 0.08f, 4.0f);
    ramp_rate_ = 1.0f / (rise_time_s * REVERB_SAMPLE_RATE);
    if (swell_dry_) {
        if (params.param2 < 0.45f) swell_dry_ = false;
    } else if (params.param2 > 0.55f) {
        swell_dry_ = true;
    }
    fdn_.PrepareBlock();
}

StereoFrame SwellReverb::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame SwellReverb::Process(StereoFrame input, const ParamSet& params) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const StereoFrame pre{pre_delay_l_.Read(), pre_delay_r_.Read()};

    // Envelope follower drives the swell ramp
    const float env = env_follow_.Process(0.5f * (pre.left + pre.right));

    if (env > 0.01f) {
        ramp_gain_ += ramp_rate_;
        if (ramp_gain_ > 1.0f) ramp_gain_ = 1.0f;
    } else {
        ramp_gain_ -= ramp_rate_ * 0.5f;
        if (ramp_gain_ < 0.0f) ramp_gain_ = 0.0f;
    }

    StereoFrame out{};
    if (!swell_dry_) {
        // Swell Wet: reverb fades in
        const StereoFrame late = fdn_.Process({pre.left * ramp_gain_, pre.right * ramp_gain_});
        out.left  = tone_[0].Process(late.left);
        out.right = tone_[1].Process(late.right);
    } else {
        // Swell Dry: reverb fades out
        const StereoFrame late = fdn_.Process(pre);
        const float scale = 1.0f - ramp_gain_;
        out.left  = tone_[0].Process(late.left  * scale);
        out.right = tone_[1].Process(late.right * scale);
    }
    return out;
}

} // namespace pedal
