#pragma once
#include <cstddef>
#include <cstdint>
#include "delay_line_sdram.h"
#include "fast_math.h"
#include "../audio/stereo_frame.h"

namespace pedal {

struct ErTap {
    uint16_t delay_samples;
    float    gain;
    float    pan;  // -1..+1 (left=-1, center=0, right=+1)
};

class EarlyReflections {
public:
    static constexpr int MAX_TAPS = 48;

    void Init(float* buf, size_t size) { line_.Init(buf, size); }
    void Reset() { line_.Reset(); }

    // Equal-power pan: angle sweeps [0, π/2] as pan goes from -1 to +1.
    // Gains are precomputed here so Process() is free of trig.
    void SetTaps(const ErTap* taps, int count) {
        tap_count_ = count < MAX_TAPS ? count : MAX_TAPS;
        for (int i = 0; i < tap_count_; ++i) {
            taps_[i] = taps[i];
            const float angle = (taps[i].pan + 1.0f) * 0.7853982f;
            gain_l_[i] = taps[i].gain * fast_cos(angle);
            gain_r_[i] = taps[i].gain * fast_sin(angle);
        }
    }

    StereoFrame Process(float input) {
        StereoFrame out{};
        for (int i = 0; i < tap_count_; ++i) {
            // Delays are integer samples — ReadNearest is lossless vs Hermite here.
            const float s = line_.ReadNearest(static_cast<float>(taps_[i].delay_samples));
            out.left  += s * gain_l_[i];
            out.right += s * gain_r_[i];
        }
        line_.Write(input);
        return out;
    }

private:
    DelayLineSdram line_;
    ErTap          taps_[MAX_TAPS]{};
    float          gain_l_[MAX_TAPS]{};
    float          gain_r_[MAX_TAPS]{};
    int            tap_count_ = 0;
};

} // namespace pedal
