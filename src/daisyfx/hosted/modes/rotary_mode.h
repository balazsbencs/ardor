#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/saturation.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/svf.h"
#include "../dsp/delay_line_sdram.h"
#include <cstddef>

namespace pedal {

/// Leslie rotating speaker simulation.
/// Horn (HF) and drum (LF) rotate at independent speeds with motor inertia.
/// P2: slow/fast switch (chorale ↔ tremolo); motor ramps with physical inertia.
class RotaryMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Rotary"; }

private:
    // Horn: max ~5ms = 240 samples; Drum: max ~10ms = 480 samples
    static constexpr size_t kHornBufSize = 256;
    static constexpr size_t kDrumBufSize = 512;

    Lfo        horn_lfo_;       // fast rotor, in-phase
    Lfo        horn_lfo_q_;     // fast rotor, 90° quadrature
    Lfo        drum_lfo_;       // slow rotor, in-phase
    Lfo        drum_lfo_q_;     // slow rotor, 90° quadrature
    Saturation drive_;
    DcBlocker  dc_l_, dc_r_;
    Svf        xover_;          // LP = drum band, HP = horn band
    Svf        horn_color_l_;   // resonant peak for horn cabinet (L)
    Svf        horn_color_r_;   // resonant peak for horn cabinet (R)

    float actual_horn_rate_ = 0.67f;
    float actual_drum_rate_ = 0.40f;
    float am_depth_  = 0.0f;
    float horn_mod_  = 0.0f;
    float drum_mod_  = 0.0f;

    float          horn_buf_[kHornBufSize];
    float          drum_buf_[kDrumBufSize];
    DelayLineSdram horn_line_;
    DelayLineSdram drum_line_;
};

} // namespace pedal
