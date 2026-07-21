#pragma once
#include "mod_mode.h"
#include "../dsp/envelope_follower.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Envelope-triggered swell — soft attack volume ramp.
/// Speed repurposed as attack time (fast=short, slow=long).
/// P1 = release time; P2 = shimmer blend (0=dry swell, 1=add fixed-delay doubling).
class AutoSwellMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "AutoSwell"; }

private:
    // Small delay buffer for optional shimmer/doubling effect (P2)
    static constexpr size_t kSwellBufSize = 1200;

    EnvelopeFollower env_;
    DcBlocker        dc_;
    DcBlocker        dc_r_;
    ToneFilter       tone_l_;
    ToneFilter       tone_r_;

    float swell_gain_  = 0.0f;   // current swell gain 0..1
    float swell_coef_  = 0.0f;   // IIR coef for gain rise (signal absent → swell opens)
    float duck_coef_   = 0.0f;   // IIR coef for gain fall (signal present → gain killed)
    float thresh_env_  = 0.05f;  // adaptive threshold tracker

    float          swell_buf_[kSwellBufSize];
    DelayLineSdram swell_line_;
};

} // namespace pedal
