#include "swell_reverb.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

void SwellReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    Fdn::Config fdn_cfg{};
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
    const size_t fdn_sizes[4] = {2522, 3080, 3660, 4232};
    for (int i = 0; i < 4; ++i) fdn_cfg.buffer_sizes[i] = fdn_sizes[i];
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(2.0f);
    fdn_.SetDamping(0.3f);

    tone_[0].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    tone_[1].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    env_follow_.Init(5.0f, 100.0f, REVERB_SAMPLE_RATE);

    ramp_gain_ = 0.0f;
    swell_dry_ = true;
    direction_seeded_ = false;
    direction_mix_ = 1.0f;
    ramp_rate_ = 1.0f / (0.5f * REVERB_SAMPLE_RATE);
}

void SwellReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    tone_[1].Init(REVERB_SAMPLE_RATE, ToneGain::Loudness);
    env_follow_.Init(5.0f, 100.0f, REVERB_SAMPLE_RATE);
    ramp_gain_ = 0.0f;
    swell_dry_ = true;
    direction_seeded_ = false;
    direction_mix_ = 1.0f;
}

void SwellReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDampFromRt60Ratio(params.decay, 0.25f + params.tone * 0.75f);
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
    if (!direction_seeded_) direction_mix_ = swell_dry_ ? 1.0f : 0.0f;
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
    const float env = env_follow_.Process(std::max(std::fabs(pre.left), std::fabs(pre.right)));

    if (env > 0.01f) {
        ramp_gain_ += ramp_rate_;
        if (ramp_gain_ > 1.0f) ramp_gain_ = 1.0f;
    } else {
        ramp_gain_ -= ramp_rate_ * 0.5f;
        if (ramp_gain_ < 0.0f) ramp_gain_ = 0.0f;
    }

    direction_seeded_ = true;
    const float target = swell_dry_ ? 1.0f : 0.0f;
    static constexpr float kDirectionStep = 1.0f / (0.020f * REVERB_SAMPLE_RATE);
    direction_mix_ += std::clamp(target - direction_mix_, -kDirectionStep, kDirectionStep);

    // At either end these are the original wet-swell and wet-duck paths.
    // Moving both gains gradually avoids an abrupt change of tank excitation
    // or output level when Direction crosses its selector boundary.
    const float input_gain = ramp_gain_ + direction_mix_ * (1.0f - ramp_gain_);
    const StereoFrame late = fdn_.Process({pre.left * input_gain, pre.right * input_gain});
    const float output_gain = 1.0f - direction_mix_ * ramp_gain_;
    return {
        tone_[0].Process(late.left * output_gain),
        tone_[1].Process(late.right * output_gain)
    };
}

} // namespace pedal
