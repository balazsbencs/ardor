#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/allpass_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// UniVibe emulation: 4 allpass stages with per-stage phase-offset LFO modulation.
/// Each stage models a distinct LDR position around the original circuit's lamp:
/// independent phase offset, center frequency, and sweep depth per stage.
/// Includes unipolar smoothstep LDR response, AM throb at 90° phase offset,
/// and mild pre-saturation for germanium transistor coloring.
class VibeMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Vibe"; }

private:
    static constexpr int kStages = 4;

    Lfo           lfo_;
    AllpassFilter stages_[kStages];
    DcBlocker     dc_;
    ToneFilter    tone_;
    float         feedback_ = 0.0f;
    float         sweep_shape_ = 0.0f;
};

} // namespace pedal
