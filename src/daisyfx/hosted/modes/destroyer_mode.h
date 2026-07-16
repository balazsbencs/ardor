#pragma once
#include "mod_mode.h"
#include "../dsp/svf.h"
#include "../dsp/dc_blocker.h"

namespace pedal {

/// Bitcrusher + decimator + resonant LP filter.
/// Speed: decimation rate (1×–48×), reduces effective sample rate.
/// Depth: bit depth reduction (16 → 1 bit).
/// Tone: post-destruction LP filter cutoff.
/// P1: filter resonance.
/// P2: vinyl noise amount (0=none, 1=max).
class DestroyerMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Destroyer"; }

private:
    Svf      svf_;
    DcBlocker dc_;
    float    held_sample_  = 0.0f;
    float    decimate_acc_ = 0.0f;
    float    decimate_rate_= 1.0f;  // samples between hold updates (= params.speed)
    int      bits_         = 16;
    uint32_t rand_         = 12345;
};

} // namespace pedal
