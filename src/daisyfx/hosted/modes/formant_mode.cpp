#include "formant_mode.h"
#include "../config/constants.h"

using namespace pedal::mod_fx;

namespace pedal {

void FormantMode::Init() {
    ff_a_.Init();
    ff_b_.Init();
    Reset();
}

void FormantMode::Reset() {
    lfo_.Init(0.5f, LfoWave::Sine);
    ff_a_.Reset();
    ff_b_.Reset();
    dc_.Init();
    blend_a_ = 1.0f;
    blend_b_ = 0.0f;
}

void FormantMode::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.speed);
    const float lfo_val = lfo_.PrepareBlock(); // block-rate morph is sufficient

    // Base vowel from P2 (0..6 → 7 vowels)
    const int base_vowel = static_cast<int>(params.p2 * 6.999f);
    const int next_vowel = (base_vowel + 1) % FormantFilter::VOWELS;

    // Morph fraction: depth and LFO position together determine blend
    float t = (0.5f + 0.5f * lfo_val) * params.depth;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    blend_a_ = 1.0f - t;
    blend_b_ = t;

    // P1 controls resonance Q: 2 = natural, 10 = focused/peaky
    const float q = 2.0f + params.p1 * 8.0f;

    ff_a_.SetVowel(base_vowel);
    ff_a_.SetResonance(q);
    ff_a_.Prepare();  // recomputes biquad coefficients — sin/cos called here, not per-sample

    ff_b_.SetVowel(next_vowel);
    ff_b_.SetResonance(q);
    ff_b_.Prepare();
}

StereoFrame FormantMode::Process(StereoFrame input, const mod_fx::ParamSet& /*params*/) {
    const float mono = input.mono();
    const float out  = ff_a_.Process(mono) * blend_a_ + ff_b_.Process(mono) * blend_b_;
    const float wet  = dc_.Process(out);
    return {wet, wet};
}

} // namespace pedal
