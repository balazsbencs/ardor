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
        clock_increment_ = 0.0f;
        input_lp_k_ = -1.0f;
        deemphasis_k_ = 0.45f;
    }

    void SetInputLpK(float k) {
        input_lp_k_ = k < 0.02f ? 0.02f : (k > 0.95f ? 0.95f : k);
        deemphasis_k_ = input_lp_k_;
    }

    /// Update the BBD clock at control rate. This removes a divide and fmodf
    /// from every sample while keeping clock noise locked to the delay time.
    void SetClockDelaySamples(float delay_samples) {
        if (delay_samples < 1.0f) delay_samples = 1.0f;
        const float fc = 24576000.0f / delay_samples;
        float f_alias = fmodf(fc, SAMPLE_RATE);
        if (f_alias > SAMPLE_RATE * 0.5f) f_alias = SAMPLE_RATE - f_alias;
        clock_increment_ = 6.2831853f * f_alias * INV_SAMPLE_RATE;
    }

    /// Process one sample through the BBD input coloration chain.
    /// Applies 2-pole LP smoothing (cut-off dynamically scales with delay time)
    /// and injects clock noise + aliased clock whine.
    /// drive_amount: 0..1, compensated nonlinear drive with subtle artifacts.
    float Process(float input, float drive_amount, uint32_t& rand_state, float delay_samples = 200.0f) {
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

        clock_phase_ += clock_increment_;
        if (clock_phase_ >= 6.2831853f) clock_phase_ -= 6.2831853f;

        // Drive is primarily nonlinear input gain. Noise remains a restrained
        // secondary BBD artifact instead of dominating the control.
        const float noise_amount = drive_amount * 0.25f;
        const float whine = fast_sin(clock_phase_) * 0.0003f * noise_amount;

        rand_state = lcg_next(rand_state);
        const float noise = lcg_to_float(rand_state) * noise_amount * 0.002f;

        // Soft saturation (tanh approximation)
        const float drive = 1.0f + drive_amount * drive_amount * 7.0f;
        const float x = lp2_ * drive + noise + whine;
        const float sat = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
        // Compensate the input gain so Drive changes harmonic density and
        // compression rather than redefining loop gain or output loudness.
        return sat / drive;
    }

    /// Apply HF shelf boost to the delay-line output.
    /// Compensates for the LP smoothing applied on the input side, restoring
    /// perceived high-frequency presence (analogous to BBD de-emphasis).
    float Deemphasis(float delayed) {
        hp_ += deemphasis_k_ * (delayed - hp_);
        return delayed + (delayed - hp_) * 0.3f; // shelf boost
    }

private:
    float lp1_ = 0.0f;
    float lp2_ = 0.0f;
    float hp_  = 0.0f;
    float clock_phase_ = 0.0f;
    float clock_increment_ = 0.0f;
    float input_lp_k_ = -1.0f;
    float deemphasis_k_ = 0.45f;
};

} // namespace pedal
