#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/svf.h"
#include "../dsp/envelope_follower.h"
#include "../dsp/dc_blocker.h"

namespace pedal {

/// Resonant filter swept by LFO or envelope follower.
/// Tone knob: 0=LP, 0.5=Wah(BP), 1=HP.
/// P1: resonance Q (0.5–20).
/// P2: waveshape (0=Sine..5=S&H, 6=Env+, 7=Env-).
class FilterMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Filter"; }

private:
    float bandOutput(const Svf& svf) const;

    Lfo              lfo_;
    Lfo              lfo_r_;   // 90° quadrature partner, for stereo width
    Svf              svf_;
    Svf              svf_r_;
    EnvelopeFollower env_;
    DcBlocker        dc_;
    DcBlocker        dc_r_;

    // Sweep is expressed as a normalised position in the shared log-frequency
    // table, not as an absolute cutoff. Filters swept linearly in frequency
    // move unevenly to the ear, and calling tanf() per sample to convert was an
    // expensive libm call in the audio path.
    float base_pos_  = 0.0f;   // centre of the sweep
    float depth_     = 0.5f;   // cached params.depth for per-sample use
    int   ftype_     = 0;      // 0=LP, 1=BP(Wah), 2=HP
    bool  use_env_   = false;
    bool  env_inv_   = false;
};

} // namespace pedal
