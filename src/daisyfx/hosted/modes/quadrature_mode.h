#pragma once
#include "mod_mode.h"
#include "../dsp/hilbert_transform.h"
#include "../dsp/lfo.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Quadrature modulation using the analytic signal (Hilbert transform).
///
/// P2 selects sub-mode:
///   0.00–0.25  AM          — ring modulation with stereo rotation
///   0.25–0.50  FM          — pitch vibrato via Hilbert SSB (LFO sweeps carrier Hz)
///   0.50–0.75  FreqShift+  — single-sideband upward frequency shift
///   0.75–1.00  FreqShift-  — single-sideband downward frequency shift
///
/// Speed: carrier / LFO rate (Hz).
/// Depth: modulation depth; FM index up to ±80 Hz.
/// P1:   stereo width (AM) or dry blend (FreqShift).
class QuadratureMode : public ModMode {
public:
    void Init()    override;
    void Reset()   override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Quadrature"; }

private:
    HilbertTransform hilbert_;
    Lfo              lfo_;             // FM vibrato LFO
    DcBlocker        dc_;              // DC removal for AM path (left)
    DcBlocker        dc_r_;            // DC removal for AM path (right)
    ToneFilter       tone_l_;
    ToneFilter       tone_r_;
    float            carrier_phase_ = 0.0f;  // [0, 2π)
    float            phase_inc_     = 0.0f;  // radians per sample (non-FM modes)
    int              sub_mode_      = 0;     // 0=AM 1=FM 2=Shift+ 3=Shift-
};

} // namespace pedal
