#pragma once
#include "../config/constants.h"

namespace pedal {

// Broad, loudness-aware two-shelf wet-tone filter.
// knob: 0=dark tilt, 0.5=flat, 1=bright tilt.
class ToneFilter {
public:
    void Init(float sample_rate = SAMPLE_RATE);
    void Reset();
    void SetKnob(float knob);
    float Process(float sample);

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float s1 = 0.0f, s2 = 0.0f;
        float Process(float input);
        void Reset() { s1 = s2 = 0.0f; }
    };

    void ComputeShelf(bool high, float fc, float gain_db, Biquad& filter) const;

    float last_knob_ = -1.0f;
    float sample_rate_ = SAMPLE_RATE;
    float inv_sample_rate_ = INV_SAMPLE_RATE;
    float output_gain_ = 1.0f;
    bool bypass_ = true;
    Biquad low_shelf_;
    Biquad high_shelf_;
};

} // namespace pedal
