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

    /// Swing: share of each pair of steps given to the first one, 0.5..0.75.
    /// Applies to straight divisions only; a triplet has no pairs to swing.
    void SetSwing(float ratio) {
        swing_ = ratio < 0.5f ? 0.5f : (ratio > 0.75f ? 0.75f : ratio);
    }

    /// Advance one sample. Returns current step gate (0.0 or 1.0).
    float Process() {
        const int step_samples = period_ / steps_per_beat_;
        int threshold = (step_samples > 0) ? step_samples : 1;
        if (steps_per_beat_ != 3 && swing_ > 0.5f) {
            // At 50 % both halves are exactly step_samples, as without swing.
            const int pair = 2 * threshold;
            const int first = static_cast<int>(static_cast<float>(pair) * swing_ + 0.5f);
            threshold = (current_step_ & 1) ? pair - first : first;
            if (threshold < 1) threshold = 1;
        }
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
        // 16 built-in patterns stored as 16-bit bitmasks (MSB = step 0). The
        // names match kPatternNames in DaisyFxCatalog.cpp; in the default 16th
        // division each group of four bits is one beat.
        //
        // Pattern 0 was 0xFFFF, every step on, which never gated at all. It is
        // now the dotted-8th rhythm, an onset every three 16ths.
        static const uint16_t kPatterns[16] = {
            0x9249, // 0: Dotted 8ths
            0xAAAA, // 1: Sixteenths (every other 16th)
            0x8888, // 2: Quarters
            0xF0F0, // 3: Half time (a beat on, a beat off)
            0xFFF0, // 4: Bar break (three beats on, one off)
            0xF000, // 5: Downbeat
            0xCCCC, // 6: Eighths
            0xA8A8, // 7: Gallop
            0xEEEE, // 8: Three 16ths
            0x8A8A, // 9: Syncopated
            0xFEFE, // 10: Stutter
            0x9999, // 11: Push
            0xE8E8, // 12: Shuffle
            0x8E8E, // 13: Reverse shuffle
            0xF8F8, // 14: Long short
            0xB6B6, // 15: Broken
        };
        return (kPatterns[pattern] >> (15 - step)) & 1;
    }

    int      period_         = static_cast<int>(SAMPLE_RATE);
    int      sample_counter_ = 0;
    int      current_step_   = 15;  // next Process() increments to 0 → step 0 fires first
    int      pattern_idx_    = 0;
    int      steps_per_beat_ = 4;
    bool     step_active_    = true;
    float    swing_          = 0.5f;
};

} // namespace pedal
