#pragma once
#include "../config/constants.h"
#include "fast_math.h"
#include <cmath>

namespace pedal {

/// Simple bucket-brigade device emulator.
/// Models: pre/de-emphasis LP filters, clock noise injection, subtle saturation.
/// Used by ChorusMode (dBucket sub-mode) to give a vintage CE-1/CE-2 character.
class BbdEmulator {
public:
    void Reset() {
        lp1_ = 0.0f;
        lp2_ = 0.0f;
        hp_  = 0.0f;
        clock_phase_ = 0.0f;
        input_lp_k_ = -1.0f;
    }

    void SetInputLpK(float k) {
        input_lp_k_ = k < 0.10f ? 0.10f : (k > 0.50f ? 0.50f : k);
    }

    /// Process one sample through the BBD input coloration chain.
    /// Applies 2-pole LP smoothing (cut-off dynamically scales with delay time)
    /// and injects clock noise + aliased clock whine.
    /// noise_amount: 0..1, clock noise injection level.
    float Process(float input, float noise_amount, uint32_t& rand_state, float delay_samples = 200.0f) {
        // Dynamic LPF cutoff: longer delays lose more high-frequencies.
        // At 144 samples (3ms) k = 0.45 (~4.6kHz). At 960 samples (20ms) k = 0.15 (~1.2kHz).
        float k;
        if (input_lp_k_ >= 0.0f) {
            k = input_lp_k_;
        } else {
            k = 0.5f - 0.35f * ((delay_samples - 48.0f) / 912.0f);
            if (k < 0.1f) k = 0.1f;
            if (k > 0.5f) k = 0.5f;
        }

        lp1_ += k * (input - lp1_);
        lp2_ += k * (lp1_  - lp2_);

        // BBD clock whine simulation: MN3007 is a 1024-stage BBD requiring a 2-phase clock.
        // The clock frequency fc required for delay_time = delay_samples / sample_rate is:
        // fc = 1024 / (2 * delay_time) = 512 * SAMPLE_RATE / delay_samples.
        // At SAMPLE_RATE = 48000 Hz, fc = 24576000.0f / delay_samples.
        float fc = 24576000.0f / delay_samples;
        // Alias the BBD clock frequency fc into the Nyquist band [0, SAMPLE_RATE / 2]
        float f_alias = fmodf(fc, SAMPLE_RATE);
        if (f_alias > SAMPLE_RATE * 0.5f) f_alias = SAMPLE_RATE - f_alias;

        // Advance clock phase
        clock_phase_ += 6.2831853f * f_alias * INV_SAMPLE_RATE;
        if (clock_phase_ >= 6.2831853f) clock_phase_ -= 6.2831853f;

        // Generate clock whine sine and mix with white noise
        const float whine = fast_sin(clock_phase_) * 0.0003f * noise_amount;

        rand_state = lcg_next(rand_state);
        const float noise = lcg_to_float(rand_state) * noise_amount * 0.002f;

        // Soft saturation (tanh approximation)
        const float x = lp2_ + noise + whine;
        const float sat = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);

        return sat;
    }

    /// Apply HF shelf boost to the delay-line output.
    /// Compensates for the LP smoothing applied on the input side, restoring
    /// perceived high-frequency presence (analogous to BBD de-emphasis).
    float Deemphasis(float delayed) {
        constexpr float k = 0.45f;
        hp_ += k * (delayed - hp_);
        return delayed + (delayed - hp_) * 0.3f; // shelf boost
    }

private:
    float lp1_ = 0.0f;
    float lp2_ = 0.0f;
    float hp_  = 0.0f;
    float clock_phase_ = 0.0f;
    float input_lp_k_ = -1.0f;
};

} // namespace pedal
