#include "chorale_reverb.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

void ChoraleReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    formant_l_.Init(REVERB_SAMPLE_RATE);
    formant_r_.Init(REVERB_SAMPLE_RATE);
    formant_l_.SetVowel(0);
    formant_r_.SetVowel(0);
    formant_l_.SetResonance(5.0f);
    formant_r_.SetResonance(5.0f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0_;
    fdn_cfg.bufs[1]     = buf_fdn1_;
    fdn_cfg.bufs[2]     = buf_fdn2_;
    fdn_cfg.bufs[3]     = buf_fdn3_;
    fdn_cfg.delays[0]   = 1366;
    fdn_cfg.delays[1]   = 1626;
    fdn_cfg.delays[2]   = 1939;
    fdn_cfg.delays[3]   = 2247;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(3.0f);
    fdn_.SetDamping(0.25f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    resonance_mode_ = 1;
}

void ChoraleReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    formant_l_.Reset();
    formant_r_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    resonance_mode_ = 1;
}

void ChoraleReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * 4.0f);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    // Param1 is mapped to the physical vowel index 0…6 by the adapter.
    const int vowel = static_cast<int>(std::lround(params.param1));
    formant_l_.SetVowel(std::clamp(vowel, 0, 6));
    formant_r_.SetVowel(std::clamp(vowel, 0, 6));

    if (resonance_mode_ == 0) {
        if (params.param2 > 0.36f) resonance_mode_ = 1;
    } else if (resonance_mode_ == 1) {
        if (params.param2 < 0.30f) resonance_mode_ = 0;
        else if (params.param2 > 0.69f) resonance_mode_ = 2;
    } else if (params.param2 < 0.63f) {
        resonance_mode_ = 1;
    }
    const float Q = resonance_mode_ == 0 ? 2.0f : resonance_mode_ == 1 ? 5.0f : 10.0f;
    formant_l_.SetResonance(Q);
    formant_r_.SetResonance(Q);
    formant_l_.Prepare();
    formant_r_.Prepare();
    fdn_.PrepareBlock();
}

StereoFrame ChoraleReverb::Process(float input, const ParamSet& /*params*/) {
    return Process(StereoFrame{input, input}, ParamSet{});
}

StereoFrame ChoraleReverb::Process(StereoFrame input, const ParamSet& /*params*/) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const StereoFrame shaped{
        formant_l_.Process(pre_delay_l_.Read()),
        formant_r_.Process(pre_delay_r_.Read())
    };
    const StereoFrame late = fdn_.Process(shaped);

    const StereoFrame out{
        tone_[0].Process(late.left),
        tone_[1].Process(late.right)
    };
    return out;
}

} // namespace pedal
