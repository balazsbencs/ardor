#include "chorale_reverb.h"
#include "daisy_seed.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

static float DSY_SDRAM_BSS buf_pre_delay[24000];
static float DSY_SDRAM_BSS buf_fdn0[2732];
static float DSY_SDRAM_BSS buf_fdn1[3252];
static float DSY_SDRAM_BSS buf_fdn2[3878];
static float DSY_SDRAM_BSS buf_fdn3[4494];

} // namespace

void ChoraleReverb::Init() {
    pre_delay_.Init(buf_pre_delay, 24000);
    pre_delay_.SetDelay(1.0f);

    formant_.Init(REVERB_SAMPLE_RATE);
    formant_.SetVowel(0);
    formant_.SetResonance(5.0f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0;
    fdn_cfg.bufs[1]     = buf_fdn1;
    fdn_cfg.bufs[2]     = buf_fdn2;
    fdn_cfg.bufs[3]     = buf_fdn3;
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
}

void ChoraleReverb::Reset() {
    pre_delay_.Reset();
    formant_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
}

void ChoraleReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * 4.0f);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    const int vowel = static_cast<int>(params.param1 * 6.0f + 0.5f);
    formant_.SetVowel(vowel < 0 ? 0 : (vowel > 6 ? 6 : vowel));

    const float Q = (params.param2 < 0.33f) ? 2.0f
                  : (params.param2 < 0.66f) ? 5.0f
                  : 10.0f;
    formant_.SetResonance(Q);
    formant_.Prepare();
    fdn_.PrepareBlock();
}

StereoFrame ChoraleReverb::Process(float input, const ParamSet& /*params*/) {
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    // FormantFilter shapes input before entering FDN
    const float shaped = formant_.Process(pre);

    const StereoFrame late = fdn_.Process(shaped);

    const StereoFrame out{
        tone_[0].Process(late.left),
        tone_[1].Process(late.right)
    };
    return out;
}

} // namespace pedal
