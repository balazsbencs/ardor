#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/allpass_filter.h"
#include "../dsp/dc_blocker.h"

namespace pedal {

/// Classic phaser — 2/4/6/8/12/16-stage allpass chain with LFO + regen.
class PhaserMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Phaser"; }

private:
    static constexpr int kMaxStages = 16;
    Lfo           lfo_;
    Lfo           lfo2_;                  // quadrature LFO for Barber Pole (π/2 offset)
    AllpassFilter stages_[kMaxStages];    // L chain (all modes) + chainA (Barber Pole)
    AllpassFilter stages_r_[kMaxStages];  // R chain (normal modes)
    DcBlocker     dc_;
    DcBlocker     dc2_;   // Barber Pole chain B / R chain DC blocker
    // Maps a barber-pole chain's 0..1 ramp position to an allpass coefficient,
    // sweeping logarithmically across the range Tone selects.
    float sweepCoeff(float phase) const;

    float         sweep_low_  = 0.0f;   // normalised bottom of the sweep range
    float         sweep_span_ = 0.4f;   // normalised width of the sweep
    float         feedback_   = 0.0f;   // L / chainA feedback
    float         feedback2_  = 0.0f;   // Barber Pole chainB feedback
    float         feedback_r_ = 0.0f;   // R chain feedback (normal modes)
    float         barber_phase_ = 0.0f;
    float         barber_inc_   = 0.0f;
    int           num_stages_  = 4;      // active stage count (normal modes)
    bool          barber_pole_ = false;  // true when sub-mode 6 is active
};

} // namespace pedal
