#pragma once
#include "delay_line_sdram.h"

namespace pedal {

// Feedback comb filter with one-pole LP damping in the feedback path.
class CombFilter {
public:
    void Init(float* buf, size_t size) {
        line_.Init(buf, size);
        state_ = 0.0f;
    }
    void Reset() { line_.Reset(); state_ = 0.0f; }

    void SetDelay(size_t samples)  { line_.SetDelay(static_cast<float>(samples)); }
    void SetFeedback(float fb)     { feedback_ = fb; }
    // damp: one-pole LP coefficient applied in the feedback path.
    // 0 = frozen (state never updates, all HF suppressed).
    // 1 = full bypass (no LP filtering, maximum brightness).
    // Typical musical range: 0.1 (very dark) to 0.7 (bright).
    void SetDamping(float damp)    { damp_ = damp; }

    // Returns the un-damped tap (use for output mixing).
    float Process(float input) {
        const float read     = line_.Read();
        state_               = state_ + damp_ * (read - state_);  // LP in feedback
        line_.Write(input + feedback_ * state_);
        return read;
    }

private:
    DelayLineSdram line_;
    float          feedback_ = 0.5f;
    float          damp_     = 0.3f;
    float          state_    = 0.0f;
};

} // namespace pedal
