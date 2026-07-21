#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Amplitude tremolo — 3 sub-modes via p2.
class VintageTremMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "VintTrem"; }

private:
    Lfo   lfo_;
    ToneFilter tone_l_;
    ToneFilter tone_r_;
    float depth_ = 0.5f;
    float shape_ = 0.0f;
    float crossover_l_ = 0.0f;
    float crossover_r_ = 0.0f;
    int   sub_mode_ = 0;
};

} // namespace pedal
