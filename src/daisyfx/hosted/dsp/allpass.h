#pragma once
#include "delay_line_sdram.h"

namespace pedal {

class DelayAllpassFilter {
public:
    void Init(float* buf, size_t size) { line_.Init(buf, size); }
    void Reset() { line_.Reset(); }
    void SetDelay(size_t samples) { line_.SetDelay(static_cast<float>(samples)); }

    // Schroeder allpass: v = buf[read]; out = -g*x + v; buf = x + g*out
    float Process(float input, float g) {
        const float v   = line_.Read();
        const float out = -g * input + v;
        line_.Write(input + g * out);
        return out;
    }

    // Modulated allpass: delay length varies per call. Band-limited read —
    // Plate's tank allpasses use this inside the feedback loop, where cubic
    // interpolation droop (-2.5 dB at 15 kHz) compounds on every circulation.
    float ProcessMod(float input, float g, float delay_samples) {
        const float v   = line_.ReadAtHighQuality(delay_samples);
        const float out = -g * input + v;
        line_.Write(input + g * out);
        return out;
    }

private:
    DelayLineSdram line_;
};

} // namespace pedal
