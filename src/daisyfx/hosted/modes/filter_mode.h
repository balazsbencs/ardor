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
    Lfo              lfo_;
    Svf              svf_;
    EnvelopeFollower env_;
    DcBlocker        dc_;

    float base_hz_           = 1000.0f;  // cutoff center (cached from tone in Prepare)
    float depth_             = 0.5f;     // cached params.depth for per-sample use
    float envelope_cutoff_hz_ = 1000.0f; // cutoff for env modes: computed in Process(), applied in Prepare()
    int   ftype_    = 0;      // 0=LP, 1=BP(Wah), 2=HP
    bool  use_env_  = false;
    bool  env_inv_  = false;
};

} // namespace pedal
