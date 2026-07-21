#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/formant_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// 5-band vowel formant filter using FormantFilter (7 vowels, 5 biquad bands).
/// P2 selects the base vowel; LFO morphs toward the adjacent vowel at Speed rate.
/// Depth controls morph depth; P1 controls resonance Q.
class FormantMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Formant"; }

private:
    Lfo           lfo_;
    FormantFilter ff_a_;     // current vowel
    FormantFilter ff_b_;     // adjacent vowel for morph
    DcBlocker     dc_;
    // Shared mono post-formant tone stage. 0.5 is a true bypass, preserving
    // the existing vowel response at the catalog default.
    ToneFilter    tone_;
    float         blend_a_ = 1.0f;
    float         blend_b_ = 0.0f;
};

} // namespace pedal
