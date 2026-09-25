#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/bbd_emulator.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/pitch_shifter.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Chorus — 5 sub-modes (dBucket/Multi/Vibrato/Detune/Digital) via p2.
class ChorusMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Chorus"; }
    // Vibrato is wet only whatever Mix says, so the mode does its own blend.
    bool OwnsDryMix() const override { return true; }

private:
    // Chorus needs a longer delay than MAX_MOD_DELAY_SAMPLES (25ms).
    // Use 50ms = 2400 samples; SDRAM buffer cost is minimal.
    static constexpr size_t kChorusBufSize = 2400;
    static constexpr size_t kDetuneBufSize = 4096;

    Lfo         lfo_[3];          // lfo_[0]/[1]: dBucket L/R + single-voice; lfo_[2]: Multi 3rd tap
    BbdEmulator bbd_;             // dBucket L: BBD pre-coloration + deemphasis
    BbdEmulator bbd_r_;           // dBucket R: BBD pre-coloration + deemphasis
    DcBlocker   dc_;
    DcBlocker   dc_r_;
    uint32_t    rand_ = 12345;
    uint32_t    rand_r_ = 67890;
    float       delays_[3] = {};
    // Prepare() writes the targets; Process() glides the live values toward
    // them. ~20 ms at 48 kHz, fast enough to track a knob, slow enough that a
    // 128-step MIDI CC does not tick.
    static constexpr float kDelaySlew = 0.001f;

    int         sub_mode_     = 4;
    bool        delay_seeded_ = false;
    float       base_target_  = 48.0f;
    float       depth_target_ = 0.0f;
    float       base_samps_ = 48.0f;
    float       mod_depth_  = 0.0f;
    float       fb_l_       = 0.0f;  // dBucket feedback register, L line
    float       fb_r_       = 0.0f;  // dBucket feedback register, R line
    float       feedback_   = 0.0f;  // dBucket feedback coefficient
    float       width_      = 1.0f;  // wet stereo width (p3), 0 = mono
    ToneFilter  tone_l_;
    ToneFilter  tone_r_;
    // One line per channel, so a stereo source keeps its image and an
    // anti-phase one does not cancel before the delay.
    float          chorus_buf_[kChorusBufSize];
    float          chorus_buf_r_[kChorusBufSize];
    DelayLineSdram chorus_line_;
    DelayLineSdram chorus_line_r_;
    float          detune_buf_l_[kDetuneBufSize];
    float          detune_buf_r_[kDetuneBufSize];
    PitchShifter   shifter_l_;
    PitchShifter   shifter_r_;
};

} // namespace pedal
