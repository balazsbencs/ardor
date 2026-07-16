#pragma once
#include <cmath>
#include "../config/constants.h"

namespace pedal {

/// Topology-preserving transform (TPT) state-variable filter.
/// Provides LP, BP, HP, and Notch outputs simultaneously.
/// Coefficient updates are safe to call once per block from Prepare().
class Svf {
public:
    void Reset() { ic1eq_ = 0.0f; ic2eq_ = 0.0f; }

    /// Set g directly (g = tan(π·f/fs)). Avoids tanf(); use when g is already precomputed.
    void SetG(float g) noexcept { g_ = g; UpdateCoeffs(); }

    /// Set cutoff frequency in Hz. Call from Prepare(), not per-sample.
    void SetFreq(float freq_hz) {
        // Clamp to Nyquist - small margin to stay stable
        if (freq_hz < 10.0f)         freq_hz = 10.0f;
        if (freq_hz > 20000.0f)      freq_hz = 20000.0f;
        g_ = tanf(3.14159265f * freq_hz * INV_SAMPLE_RATE);
        UpdateCoeffs();
    }

    /// Set resonance Q (0.5 = critically damped, higher = more resonant).
    void SetQ(float q) {
        if (q < 0.5f)  q = 0.5f;
        if (q > 40.0f) q = 40.0f;
        k_ = 1.0f / q;
        UpdateCoeffs();
    }

    /// Process one sample. Returns LP output.
    /// After calling, lp(), bp(), hp(), notch() accessors are valid.
    float Process(float x) {
        const float v3 = x - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;
        lp_ = v2;
        bp_ = v1;
        hp_ = x - k_ * v1 - v2;
        notch_ = hp_ + lp_;
        return lp_;
    }

    float lp()    const { return lp_; }
    float bp()    const { return bp_; }
    float hp()    const { return hp_; }
    float notch() const { return notch_; }

private:
    void UpdateCoeffs() {
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    float g_     = 0.1f;
    float k_     = 1.0f;  // damping = 1/Q
    float a1_    = 0.0f;
    float a2_    = 0.0f;
    float a3_    = 0.0f;
    float ic1eq_ = 0.0f;
    float ic2eq_ = 0.0f;
    float lp_    = 0.0f;
    float bp_    = 0.0f;
    float hp_    = 0.0f;
    float notch_ = 0.0f;
};

} // namespace pedal
