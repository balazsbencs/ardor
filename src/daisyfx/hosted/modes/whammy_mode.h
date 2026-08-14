#pragma once
#include "mod_mode.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/pitch_shifter.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Pedal-swept pitch shifter in the style of a DigiTech Whammy.
///
/// Two families, selected by Depth:
///   Whammy  — one shifted voice, no dry. Heel is unison, toe is the interval.
///   Harmony — dry plus one shifted voice. The pedal morphs between two fixed
///             chromatic intervals.
///
/// P1 is the pedal position and is the control worth assigning to an expression
/// input. P2 selects the preset within the family.
///
/// The engine is the granular shifter rather than the band-shifter that drives
/// Poly Octave. That one produces its voices with double- and half-angle
/// identities, so it is structurally limited to octaves and cannot express a
/// fifth, let alone sweep continuously through one.
class WhammyMode : public ModMode {
public:
    // 10 Whammy presets followed by 9 Harmony presets, on one selector.
    static constexpr int PRESET_COUNT = 19;

    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Whammy"; }

private:
    // 8192 covers the widest upward ratio (4.0 at +24) against the grain size
    // below, and leaves history for the slowest downward read (0.125 at -36).
    static constexpr size_t kBufSize    = 8192;
    // ~21 ms. Long enough that the 47 Hz grain rate stays under the note
    // fundamental, short enough to keep the latency near the original unit.
    static constexpr size_t kGrainSize  = 1024;

    float buf_[kBufSize];
    PitchShifter shifter_;
    ToneFilter   tone_;
    DcBlocker    dc_;

    // Pedal glide, in semitones. Slewing the ratio instead would accelerate the
    // sweep toward the top of its travel, because ratio is exponential in pitch.
    float semitones_       = 0.0f;
    float semitone_target_ = 0.0f;
    float glide_           = 0.25f;

    // Ratio is ramped across the control block so a 128-step expression pedal
    // does not step the read pointer once per millisecond.
    float ratio_           = 1.0f;
    float ratio_step_      = 0.0f;

    bool  harmony_        = false;
    float harmony_level_  = 1.0f;
    int   preset_         = 0;
    bool  seeded_         = false;
};

} // namespace pedal
