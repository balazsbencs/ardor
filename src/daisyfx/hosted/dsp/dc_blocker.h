#pragma once

#include "../config/constants.h"
#include <cmath>

namespace pedal {

class DcBlocker {
public:
    // Default cutoff for a blocker that sees the signal once.
    static constexpr float DEFAULT_CUTOFF_HZ = 5.35f;
    // Cutoff for a blocker sitting inside a feedback loop. The signal passes
    // through it once per circulation — roughly 66 times in a 2 s tail with a
    // 30 ms loop — so the per-pass loss compounds. At 5.35 Hz that costs 3.2 dB
    // at 50 Hz across such a tail, which makes bass decay faster than mids. At
    // 1 Hz the same tail loses under 0.1 dB while still removing DC.
    static constexpr float FEEDBACK_LOOP_CUTOFF_HZ = 1.0f;

    // Keep the same cutoff regardless of the owning DSP clock.
    void Init(float sample_rate = SAMPLE_RATE, float cutoff_hz = DEFAULT_CUTOFF_HZ) {
        const float sr = (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : SAMPLE_RATE;
        const float fc = (cutoff_hz > 0.0f && std::isfinite(cutoff_hz)) ? cutoff_hz : DEFAULT_CUTOFF_HZ;
        r_ = std::exp(-6.28318530718f * fc / sr);
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

    inline float Process(float x) {
        float y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float x1_ = 0.0f;
    float y1_ = 0.0f;
    float r_  = 0.9993f;
};

} // namespace pedal
