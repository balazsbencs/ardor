#pragma once
#include "delay_line_sdram.h"
#include "dc_blocker.h"
#include "../audio/stereo_frame.h"
#include <cstddef>

namespace pedal {

// Feedback Delay Network: 4-line or 8-line with Hadamard mixing.
// Buffers must be DSY_SDRAM_BSS arrays owned by the caller.
class Fdn {
public:
    static constexpr int MAX_LINES = 8;
    static constexpr float MAX_MOD_DEPTH_SAMPLES = 8.0f;

    struct Config {
        // Must be 4 or 8; other values fall back to 4-line Hadamard mixing.
        int    n_lines;
        // bufs[i]: DSY_SDRAM_BSS float array with at least delays[i] elements.
        // Indices >= n_lines must be nullptr (unused lines are not touched).
        float* bufs[MAX_LINES];
        // delays[i]: delay length in samples; must be >= 1 for active lines.
        // Minimum buffer size for line i: delays[i] * sizeof(float) bytes.
        // Indices >= n_lines must be 0.
        size_t delays[MAX_LINES];
        float  sample_rate;
    };

    void Init(const Config& cfg);
    void Reset();

    // Sets per-line feedback gains for the target RT60 decay time.
    void SetDecay(float decay_s);

    // One-pole LP coefficient in feedback path (1=bright/bypass, lower=more damping).
    void SetDamping(float damp);

    // Compute damping from RT60 targets. hf_ratio: HF RT60 = rt60_lf_s * hf_ratio.
    // 0.3 = very dark (HF decays 3× faster), 1.0 = uniform (all frequencies decay equally).
    void SetDampFromRt60Ratio(float rt60_lf_s, float hf_ratio);

    // Per-line LFO modulation depth in samples (0 = no modulation).
    void SetModulation(float depth_samples);

    // Advance LFO phases one block and cache modulated tap positions.
    // Must be called once per block (e.g. at end of Prepare()) before Process().
    void PrepareBlock();

    // Hold: freeze feedback gains at 1.0 (infinite sustain).
    void SetHold(bool hold);

    StereoFrame Process(float input);
    StereoFrame Process(StereoFrame input);

private:
    void hadamard4(float v[4]) const;
    void hadamard8(float v[8]) const;

    DelayLineSdram lines_[MAX_LINES];
    DcBlocker      dc_[MAX_LINES];               // per-line DC blocker in feedback path
    float          delay_s_[MAX_LINES]{};        // per-line delay in seconds
    float          delay_samples_[MAX_LINES]{};  // per-line delay in samples (for modulated ReadAt)
    float          feedback_[MAX_LINES]{};       // nominal feedback gains
    float          lp_state_[MAX_LINES]{};       // one-pole LP state
    float          lfo_phase_[MAX_LINES]{};
    float          modulated_delay_[MAX_LINES]{};  // cached per-block tap positions
    float          damp_        = 0.3f;
    float          mod_depth_   = 0.0f;
    float          sample_rate_ = 48000.0f;
    int            n_lines_     = 4;
    bool           hold_        = false;
};

} // namespace pedal
