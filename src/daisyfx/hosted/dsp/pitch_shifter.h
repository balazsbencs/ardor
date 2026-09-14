#pragma once
#include <cstddef>

namespace pedal {

// Granular pitch shifter using overlapping Hann-windowed grains.
// buf must be caller-owned float storage of size buf_size.
// Minimum recommended buf_size = grain_size * max(1, ratio_max).
class PitchShifter {
public:
    static constexpr size_t GRAIN_SIZE = 2048;   // default, kept for existing callers
    static constexpr int    GRAINS = 2;

    // Two grains at 50% overlap. More grains is NOT better here: naive
    // overlap-add sums mutually incoherent segments, so a four-grain version
    // measured 9 to 34 dB down through partial cancellation, worst on ratios
    // furthest from a simple one. Grain count stays fixed; grain size is the
    // useful control, trading smearing against latency and grain-rate roughness.
    void  Init(float* buf, size_t buf_size, float sample_rate = 48000.0f,
               size_t grain_size = GRAIN_SIZE);
    void  Reset();

    // Control rate: converts semitones to a ratio and retunes the anti-alias
    // filter. Costs a pow() and an exp().
    void  SetShift(float semitones);  // range: -36..+24

    // Audio rate: sets the ratio directly with no transcendental calls, for
    // ramping between control updates. The anti-alias filter is left where
    // SetShift() put it, which is correct across one control block.
    void  SetRatioFast(float ratio) { ratio_ = clampRatio(ratio); }

    float Ratio() const { return ratio_; }
    float Process(float input);

private:
    float  ReadInterp(float pos) const;
    void   UpdateAntiAlias();
    float  clampRatio(float ratio) const;
    float  FindRestart(float nominal, float trailing_from) const;
    float  At(long index) const;

    // Waveform-similarity search bounds.
    //
    // Restarting a grain at a fixed distance leaves a phase discontinuity
    // whenever that distance is not a whole number of input periods. The
    // leftover phase biases the output pitch by (leftover cycles x grain rate),
    // which is why a fixed-jump shifter detunes by an amount that depends on
    // the note being played — measured from -923 to +829 cents across the
    // guitar range. Searching back up to one low-E period for the restart point
    // whose run-up best matches the outgoing grain removes the discontinuity,
    // and with it the detune.
    static constexpr int kSearchSpan  = 700;  // samples; covers down to ~69 Hz
    static constexpr int kMatchWindow = 256;  // trailing context compared
    static constexpr int kMatchStride = 4;    // decimation within that window
    static constexpr int kCoarseStride = 8;   // first-pass step, refined after
    // Below this jump size there is no meaningful discontinuity to repair.
    static constexpr float kMinJumpForSearch = 8.0f;

    float*  buf_         = nullptr;
    size_t  buf_size_    = 0;
    size_t  write_pos_   = 0;
    size_t  grain_size_  = GRAIN_SIZE;
    float   read_pos_[GRAINS] = {};
    float   grain_phase_[GRAINS] = {};
    float   ratio_       = 1.0f;
    float   sample_rate_ = 48000.0f;

    struct AntiAliasBiquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float s1 = 0.0f, s2 = 0.0f;
        float Process(float input) {
            const float output = b0 * input + s1;
            s1 = b1 * input - a1 * output + s2;
            s2 = b2 * input - a2 * output;
            return output;
        }
        void Reset() { s1 = s2 = 0.0f; }
    };

    // Upward reading is decimation. A fourth-order Butterworth prefilter gives
    // the shimmer feedback meaningful rejection above its new Nyquist limit.
    AntiAliasBiquad aa_[2];
    bool aa_active_ = false;
};

} // namespace pedal
