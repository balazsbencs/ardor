#pragma once

#include "delay_line_sdram.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pedal {

// Feed-forward delay for controls that can jump while audio is running. Keep
// the delay history intact and crossfade between two stationary read heads;
// moving one head through the buffer would pitch-shift the input on a sweep.
class ClicklessDelayLine {
public:
    void Init(float* buffer, size_t size) {
        line_.Init(buffer, size);
        maximum_delay_ = static_cast<float>(size > 3 ? size - 3 : 2);
        current_delay_ = target_delay_ = requested_delay_ = 2.0f;
        fade_remaining_ = 0;
        seeded_ = false;
    }

    void Reset() {
        line_.Reset();
        current_delay_ = target_delay_ = requested_delay_;
        line_.SetDelay(current_delay_);
        fade_remaining_ = 0;
        seeded_ = false;
    }

    void SetDelay(float delay_samples) {
        if (!std::isfinite(delay_samples)) return;
        requested_delay_ = std::clamp(delay_samples, 2.0f, maximum_delay_);
        if (!seeded_) {
            current_delay_ = target_delay_ = requested_delay_;
            line_.SetDelay(current_delay_);
        } else if (fade_remaining_ == 0 && requested_delay_ != current_delay_) {
            target_delay_ = requested_delay_;
            fade_remaining_ = kFadeSamples;
        }
    }

    void Write(float sample) { line_.Write(sample); }

    float Read() {
        seeded_ = true;
        if (fade_remaining_ == 0) return line_.Read();

        const float blend = static_cast<float>(kFadeSamples - fade_remaining_ + 1)
                          / static_cast<float>(kFadeSamples);
        const float old_tap = line_.ReadAt(current_delay_);
        const float new_tap = line_.ReadAt(target_delay_);
        const float output = old_tap + blend * (new_tap - old_tap);
        if (--fade_remaining_ == 0) {
            current_delay_ = target_delay_;
            line_.SetDelay(current_delay_);
            if (requested_delay_ != current_delay_) {
                target_delay_ = requested_delay_;
                fade_remaining_ = kFadeSamples;
            }
        }
        return output;
    }

private:
    static constexpr int kFadeSamples = 480;
    DelayLineSdram line_;
    float maximum_delay_ = 2.0f;
    float current_delay_ = 2.0f;
    float target_delay_ = 2.0f;
    float requested_delay_ = 2.0f;
    int fade_remaining_ = 0;
    bool seeded_ = false;
};

} // namespace pedal
