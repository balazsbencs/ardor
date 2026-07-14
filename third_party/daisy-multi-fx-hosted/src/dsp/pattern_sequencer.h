#pragma once
#include "../config/constants.h"
#include <cstdint>

namespace pedal {

/// 16-step pattern sequencer for PatternTremMode.
/// Caller drives it per-sample; it fires a "step" event when the beat advances.
/// 16 built-in patterns (index 0–15); each step is either ON (1) or OFF (0).
class PatternSequencer {
public:
    void Reset() {
        sample_counter_ = 0;
        current_step_   = 15;  // next Process() increments to 0 → step 0 fires first
        step_active_    = true;
    }

    /// Set the period of one beat in samples (48000 = 1 beat/sec).
    void SetPeriodSamples(float samples_per_beat) {
        period_ = (samples_per_beat > 1.0f)
            ? static_cast<int>(samples_per_beat + 0.5f) : 1;
    }

    /// Set active pattern (0–15) and subdivision (1, 2, or 3 = triplet).
    void SetPattern(int pattern_idx, int steps_per_beat) {
        pattern_idx_    = pattern_idx  & 0xF;
        steps_per_beat_ = (steps_per_beat < 1) ? 1
                        : (steps_per_beat > 16) ? 16 : steps_per_beat;
    }

    /// Advance one sample. Returns current step gate (0.0 or 1.0).
    float Process() {
        const int step_samples = period_ / steps_per_beat_;
        const int threshold = (step_samples > 0) ? step_samples : 1;
        ++sample_counter_;
        if (sample_counter_ >= threshold) {
            sample_counter_ -= threshold;
            current_step_ = (current_step_ + 1) % 16;
            step_active_  = GetStep(pattern_idx_, current_step_);
        }
        return step_active_ ? 1.0f : 0.0f;
    }

    int CurrentStep() const { return current_step_; }

private:
    static bool GetStep(int pattern, int step) {
        // 16 built-in patterns stored as 16-bit bitmasks (MSB = step 0)
        static const uint16_t kPatterns[16] = {
            0xFFFF, // 0: all on
            0xAAAA, // 1: every other (8th notes)
            0x8888, // 2: quarter notes
            0xF0F0, // 3: half-note on/off
            0xFFF0, // 4: 3 on, 1 off
            0xF000, // 5: first quarter only
            0xCCCC, // 6: dotted-8th pattern
            0xA8A8, // 7: syncopated
            0xEEEE, // 8: 3+1 groups
            0x8A8A, // 9: sparse syncopation
            0xFEFE, // 10: accent first beat
            0x9999, // 11: sparse even
            0xE8E8, // 12: swung-8th feel
            0x8E8E, // 13: swung reverse
            0xF8F8, // 14: strong downbeats
            0xB6B6, // 15: complex
        };
        return (kPatterns[pattern] >> (15 - step)) & 1;
    }

    int      period_         = static_cast<int>(SAMPLE_RATE);
    int      sample_counter_ = 0;
    int      current_step_   = 15;  // next Process() increments to 0 → step 0 fires first
    int      pattern_idx_    = 0;
    int      steps_per_beat_ = 4;
    bool     step_active_    = true;
};

} // namespace pedal
