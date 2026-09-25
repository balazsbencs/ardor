#pragma once
#include "mod_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/delay_line_sdram.h"
#include <cstring>

namespace pedal {

// A tiny, self-contained 1-pole low pass filter to simulate analog BBD warmth.
struct SimpleLpf {
    float state = 0.0f;
    float coeff = 0.5f;

    void Init(float c = 0.5f) {
        state = 0.0f;
        coeff = c;
    }

    float Process(float input) {
        state = state + coeff * (input - state);
        return state;
    }
};

// A small, highly efficient integer delay line for the pure dry path in TZF.
struct DryDelay {
    float  buf[256];
    size_t write;

    void Init() {
        std::memset(buf, 0, sizeof(buf));
        write = 0;
    }

    void Write(float sample) {
        buf[write] = sample;
        write = (write + 1) & 255;
    }

    float Read(size_t delay_samples) const {
        size_t read_ptr = (write + 255 - delay_samples) & 255;
        return buf[read_ptr];
    }
};

/// Flanger — short delay + feedback with LFO modulation.
/// 6 sub-modes via p2: Silver/Grey/Black+/Black-/Zero+/Zero-
class FlangerMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Flanger"; }
    // The through-zero types delay their dry path to meet the wet tap, so the
    // flanger blends dry and wet itself for every type.
    bool OwnsDryMix() const override { return true; }

private:
    // 10ms max delay = 480 samples + headroom
    static constexpr size_t kFlangerBufSize = 512;
    // Shortest tap the 16-tap sinc read can serve: it needs seven samples
    // newer than the tap. Asking for less is silently clamped there, which
    // left a flat spot at the bottom of every deep sweep.
    static constexpr float kMinDelay = 7.0f;

    Lfo       lfo_;
    Lfo       lfo_r_;  // right channel LFO, offset by π/2 for stereo spread

    // Stereo DC Blockers
    DcBlocker dc_l_;
    DcBlocker dc_r_;

    // Feedback Low-Pass Filters for analog warmth using our new struct
    SimpleLpf fb_lpf_l_;
    SimpleLpf fb_lpf_r_;

    // Pure dry delay lines for through-zero flanging
    DryDelay  dry_delay_l_;
    DryDelay  dry_delay_r_;

    float     max_depth_ = 240.0f;  // max delay swing for current sub-mode
    float     depth_     = 0.5f;    // cached params.depth
    // Manual: the sweep centre, as a fraction of max_depth_. Prepare() sets
    // the target and Process() glides to it (~20 ms), so turning Manual
    // sweeps smoothly instead of stepping the tap.
    static constexpr float kCentreSlew = 0.001f;
    float     centre_target_ = 120.0f;
    float     centre_        = 120.0f;
    bool      centre_seeded_ = false;
    // Stereo: how far the R drift follows its own random walk (0 = shares L).
    float     drift_spread_  = 1.0f;
    float     fb_sign_   = 1.0f;    // +1 or -1 from sub-mode

    // Wow and flutter. kDriftCoeff sets the rate (~1.5 Hz at 48 kHz) and
    // kDriftGain scales the settled deviation to roughly +/-2 samples, which is
    // audible as tape drift without becoming a chorus.
    static constexpr float kDriftCoeff = 0.0002f;
    static constexpr float kDriftGain  = 340.0f;

    // Random walk / noise state for wow & flutter drift emulation
    uint32_t  rand_state_ = 12345;
    float     drift_l_    = 0.0f;
    float     drift_r_    = 0.0f;

    float          flanger_buf_l_[kFlangerBufSize];
    float          flanger_buf_r_[kFlangerBufSize];
    DelayLineSdram flanger_line_l_;
    DelayLineSdram flanger_line_r_;
};

} // namespace pedal
