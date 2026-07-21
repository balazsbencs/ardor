#pragma once

#include "../config/constants.h"
#include <cmath>

namespace pedal {

class DcBlocker {
public:
    // Keep the same ~5 Hz cutoff regardless of the owning DSP clock.
    void Init(float sample_rate = SAMPLE_RATE) {
        const float sr = (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : SAMPLE_RATE;
        r_ = std::exp(-6.28318530718f * 5.35f / sr);
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
