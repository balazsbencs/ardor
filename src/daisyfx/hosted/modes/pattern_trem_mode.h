#pragma once
#include "mod_mode.h"
#include "../dsp/pattern_sequencer.h"

namespace pedal {

/// Rhythmic tremolo driven by a 16-step pattern sequencer.
/// P1 selects pattern (0–15). P2 selects subdivision (straight/triplet/dotted).
/// Speed repurposed as BPM rate (1–10 Hz → 60–600 BPM equivalent).
class PatternTremMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "PattTrem"; }

private:
    PatternSequencer seq_;
    float            gate_      = 0.0f;   // current gate value (0 or 1)
    float            smoothed_  = 0.0f;   // smoothed gate (attack/release)
};

} // namespace pedal
