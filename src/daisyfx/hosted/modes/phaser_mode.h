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
    // Normal modes: stages_ is the L chain and stages_r_ the R chain.
    // Barber Pole: stages_[0..3] / [4..7] are chains A / B for L, and
    // stages_r_[0..3] / [4..7] the same pair for R.
    AllpassFilter stages_[kMaxStages];
    AllpassFilter stages_r_[kMaxStages];
    DcBlocker     dc_;
    DcBlocker     dc2_;   // Barber Pole L chain B / normal R chain
    DcBlocker     dc3_;   // Barber Pole R chain A
    DcBlocker     dc4_;   // Barber Pole R chain B
    // Maps a barber-pole chain's 0..1 ramp position to an allpass coefficient,
    // sweeping logarithmically across the range Tone selects.
    float sweepCoeff(float phase) const;

    float         sweep_low_  = 0.0f;   // normalised bottom of the sweep range
    float         sweep_span_ = 0.4f;   // normalised width of the sweep
    float         polarity_   = 1.0f;   // regen sign: +1 or -1 (p4)
    float         feedback_   = 0.0f;   // L / chainA feedback
    float         feedback2_  = 0.0f;   // Barber Pole chainB feedback
    float         feedback_r_ = 0.0f;   // R chain feedback (normal) / Barber R chainA
    float         feedback2_r_ = 0.0f;  // Barber Pole R chainB feedback
    float         barber_phase_ = 0.0f;
    float         barber_inc_   = 0.0f;
    int           num_stages_  = 4;      // active stage count (normal modes)
    bool          barber_pole_ = false;  // true when sub-mode 6 is active
};

} // namespace pedal
