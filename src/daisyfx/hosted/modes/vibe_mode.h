#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/allpass_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Uni-Vibe emulation: one lamp lights four photocells, each setting the
/// corner of one allpass stage. The lamp's filament lags the oscillator, the
/// cells answer light quickly and darkness slowly, and each stage's corner
/// follows the cell's resistance as a power law. That chain, not the
/// oscillator, gives the Vibe its lopsided, speed-dependent throb.
/// Includes AM throb from the same lamp and mild pre-saturation for
/// germanium transistor coloring.
class VibeMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Vibe"; }

private:
    static constexpr int kStages = 4;

    // One chain per channel. Both share the lamp, so the sweep is the same;
    // a stereo source keeps its image and an anti-phase one does not cancel.
    struct Channel {
        AllpassFilter stages[kStages];
        DcBlocker     dc;
        ToneFilter    tone;
        float         feedback = 0.0f;
    };
    float ProcessChannel(Channel& channel, float input, const float* coeffs,
                         float regen, float am_gain);

    Lfo           lfo_;
    Channel       channels_[2];
    float         stage_centre_[kStages]{};
    float         lamp_ = 0.0f;      // filament temperature (filtered drive power)
    float         cell_ = 0.0f;      // photocell conductance, shared by the four cells
    float         cell_release_ = 0.0f;  // release coefficient, from Lag (p2)
};

} // namespace pedal
