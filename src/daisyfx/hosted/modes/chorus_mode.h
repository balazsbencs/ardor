#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/bbd_emulator.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/pitch_shifter.h"

namespace pedal {

/// Chorus — 5 sub-modes (dBucket/Multi/Vibrato/Detune/Digital) via p2.
class ChorusMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Chorus"; }

private:
    // Chorus needs a longer delay than MAX_MOD_DELAY_SAMPLES (25ms).
    // Use 50ms = 2400 samples; SDRAM buffer cost is minimal.
    static constexpr size_t kChorusBufSize = 2400;
    static constexpr size_t kDetuneBufSize = 4096;

    Lfo         lfo_[3];          // lfo_[0]/[1]: dBucket L/R + single-voice; lfo_[2]: Multi 3rd tap
    BbdEmulator bbd_;             // BBD pre-coloration + L deemphasis
    BbdEmulator bbd_r_;           // separate deemphasis state for dBucket R channel
    DcBlocker   dc_;
    DcBlocker   dc_r_;
    uint32_t    rand_ = 12345;
    float       delays_[3] = {};
    int         sub_mode_   = 4;
    float       base_samps_ = 48.0f;
    float       mod_depth_  = 0.0f;
    float       fb_samp_    = 0.0f;  // feedback register for dBucket
    float       feedback_   = 0.0f;  // feedback coefficient (set from tone in Prepare)
    float          chorus_buf_[kChorusBufSize];
    DelayLineSdram chorus_line_;
    float          detune_buf_l_[kDetuneBufSize];
    float          detune_buf_r_[kDetuneBufSize];
    PitchShifter   shifter_l_;
    PitchShifter   shifter_r_;
};

} // namespace pedal
