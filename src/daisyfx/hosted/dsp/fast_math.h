#pragma once
#include <cmath>
#include <cstdint>

namespace pedal {

/// Fast sine approximation via a 7th-order polynomial.
///
/// Accuracy: max error < 0.016 % over a full cycle. Using this instead of libm
/// sinf saves flash and avoids an expensive library call in audio-rate LFOs
/// and FDN modulation.
/// because sinf.o (and cosf.o) are no longer pulled from the math library.
///
/// @param x  Phase in radians.  Expected range [0, 2π); values outside
///           this range give incorrect results.
inline float fast_sin(float x) noexcept {
    // Fold [π, 2π) down to [0, π), tracking the sign change.
    float sign = 1.0f;
    if (x > 3.14159265f) {
        x    -= 3.14159265f;
        sign  = -1.0f;
    }
    // Fold [π/2, π] to [0, π/2] using the identity sin(π − x) = sin(x).
    if (x > 1.57079633f) x = 3.14159265f - x;
    // 7th-order Taylor polynomial, factored for four multiplies after x².
    // On [0, pi/2] it remains within [-1, 1], so it cannot overdrive an LFO.
    const float x2 = x * x;
    return sign * x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841270f)));
}

/// Fast cosine for x ∈ [0, π/2].
/// Uses the identity cos(x) = sin(π/2 − x).
/// Only valid for the stated range; used for the equal-power mix crossfade.
inline float fast_cos(float x) noexcept {
    return fast_sin(1.57079633f - x);
}

/// Fast cosine over a full cycle, x ∈ [0, 2π).
/// cos(x) = sin(x + π/2), wrapped back into fast_sin's valid range.
inline float fast_cos_full(float x) noexcept {
    x += 1.57079633f;
    if (x >= 6.28318531f) x -= 6.28318531f;
    return fast_sin(x);
}

/// Numerical Recipes LCG — advances a 32-bit PRNG state in-place.
inline uint32_t lcg_next(uint32_t state) noexcept {
    return state * 1664525u + 1013904223u;
}

/// Maps a raw LCG state to a signed float in [-1, +1].
inline float lcg_to_float(uint32_t state) noexcept {
    return static_cast<float>(static_cast<int32_t>(state)) * (1.0f / 2147483648.0f);
}

/// Fast tanh-like soft clip using a Padé approximation.
inline float soft_clip_tanh(float x) noexcept {
    if (x <= -3.0f) return -1.0f;
    if (x >=  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/// Clean below `knee`, then soft-limit peaks toward full scale.
inline float soft_limit_above(float x, float knee) noexcept {
    const float magnitude = x < 0.0f ? -x : x;
    if (magnitude <= knee) return x;
    const float limited = knee + (1.0f - knee) * soft_clip_tanh((magnitude - knee) / (1.0f - knee));
    return x < 0.0f ? -limited : limited;
}

// Unity-slope soft ceiling. Leaves ordinary samples untouched, while keeping
// a coherent multi-tap sum inside full scale without a hard clipping corner.
inline float soft_headroom(float x) noexcept {
    const float magnitude = std::abs(x);
    if (magnitude <= 0.8f) return x;
    const float over = magnitude - 0.8f;
    const float bounded = 0.8f + 0.2f * over / (0.2f + over);
    return x < 0.0f ? -bounded : bounded;
}

/// Antiderivative of soft_clip_tanh, for the anti-aliased version below.
///
/// Inside the knee the curve is (1/9)(x + 24x/(x^2+3)), which integrates to
/// (1/9)(x^2/2 + 12 log1p(x^2/3)) after removing the constant. Beyond +/-3
/// the curve is flat; the outer integral meets the inner one continuously.
inline double soft_clip_tanh_integral(double x) noexcept {
    const double ax = std::abs(x);
    // Remove the arbitrary integral constant. Near silence, subtracting two
    // large float logarithms overwhelmed the wanted signal.
    constexpr double kOutsideOffset = (4.5 + 12.0 * 1.3862943611198906) / 9.0 - 3.0;
    if (ax >= 3.0) return ax + kOutsideOffset;
    const double x2 = x * x;
    return (0.5 * x2 + 12.0 * std::log1p(x2 / 3.0)) / 9.0;
}

/// soft_clip_tanh with first-order antiderivative anti-aliasing.
///
/// A waveshaper run per sample folds the harmonics it makes back across
/// Nyquist. The usual answer is to oversample it, but these saturators sit
/// inside feedback loops, and oversampling a loop means oversampling the delay
/// line reads in it too — which here are 16-tap sinc interpolations. Averaging
/// the curve across the segment between this sample and the last one costs a
/// logarithm instead, and needs no resampling at all.
///
/// Only worth it where something drives the curve well past its knee. Measured
/// on a 5 kHz tone, the plain shaper aliases at -70 dBc when the signal reaches
/// its input at unity, which is where most callers leave it, and at -19 dBc by
/// the time a drive control has multiplied it by eight.
///
/// Costs a half sample of group delay, which is why it is opt-in rather than
/// folded into soft_clip_tanh itself.
class AntiAliasedSoftClip {
public:
    void Reset() noexcept {
        previous_ = 0.0f;
        previous_integral_ = soft_clip_tanh_integral(0.0f);
    }

    float Process(float x) noexcept {
        const double integral = soft_clip_tanh_integral(x);
        const double delta = static_cast<double>(x) - previous_;
        // Across a segment too short to divide by, fall back to the midpoint of
        // the curve, which is what the average tends to anyway.
        const float out = (delta > 1.0e-5 || delta < -1.0e-5)
            ? (integral - previous_integral_) / delta
            : soft_clip_tanh(0.5f * (x + static_cast<float>(previous_)));
        previous_ = x;
        previous_integral_ = integral;
        return out;
    }

private:
    double previous_ = 0.0;
    double previous_integral_ = soft_clip_tanh_integral(0.0);
};

// A safety limiter that retains its native-rate, exactly clean path below the
// knee. Once a peak approaches the knee, blend toward antiderivative
// antialiasing. A short peak hold prevents the blend from following individual
// waveform cycles and creating sidebands of its own.
class AntiAliasedSoftLimit {
public:
    void Init(float sample_rate) noexcept {
        hold_frames_ = static_cast<int>(sample_rate * 0.02f);
        release_coefficient_ = std::exp(-1.0f / (sample_rate * 0.01f));
        Reset();
    }

    void Reset() noexcept {
        previous_ = previous_integral_ = 0.0;
        peak_ = 0.0f;
        hold_remaining_ = 0;
    }

    float Process(float input, float knee = 0.7f) noexcept {
        const float magnitude = std::abs(input);
        if (magnitude >= peak_) {
            peak_ = magnitude;
            hold_remaining_ = hold_frames_;
        } else if (hold_remaining_ > 0) {
            --hold_remaining_;
        } else {
            peak_ *= release_coefficient_;
        }

        const double x = input;
        const double integral = Integral(x, knee);
        const double delta = x - previous_;
        const float plain = soft_limit_above(input, knee);
        float output = plain;
        if (peak_ > knee) {
            const float mix = std::fmin((peak_ - knee) / (1.0f - knee), 1.0f);
            const float antialiased = std::abs(delta) > 1.0e-5
                ? static_cast<float>((integral - previous_integral_) / delta)
                : soft_limit_above(static_cast<float>(0.5 * (x + previous_)), knee);
            output += mix * (antialiased - plain);
        }
        previous_ = x;
        previous_integral_ = integral;
        return output;
    }

private:
    static double Integral(double x, double knee) noexcept {
        const double magnitude = std::abs(x);
        if (magnitude <= knee) return 0.5 * x * x;
        const double over = magnitude - knee;
        const double range = 1.0 - knee;
        return 0.5 * knee * knee + knee * over +
               range * range * soft_clip_tanh_integral(over / range);
    }

    double previous_ = 0.0;
    double previous_integral_ = 0.0;
    float peak_ = 0.0f;
    float release_coefficient_ = 0.0f;
    int hold_frames_ = 0;
    int hold_remaining_ = 0;
};

// First-order antiderivative antialiasing for x/(1+|x|). Output is divided
// by the requested drive, so the feedback loop retains unity small-signal
// gain while the curve compresses stronger repeats.
class AntiAliasedTapeClip {
public:
    void Reset() noexcept { previous_ = previous_integral_ = 0.0; }

    float Process(float input, float drive) noexcept {
        const double x = static_cast<double>(input) * drive;
        const double magnitude = std::abs(x);
        const double integral = magnitude - std::log1p(magnitude);
        const double delta = x - previous_;
        const double shaped = std::abs(delta) > 1.0e-5
            ? (integral - previous_integral_) / delta
            : 0.5 * (x + previous_) / (1.0 + std::abs(0.5 * (x + previous_)));
        previous_ = x;
        previous_integral_ = integral;
        return static_cast<float>(shaped / drive);
    }

private:
    double previous_ = 0.0;
    double previous_integral_ = 0.0;
};

} // namespace pedal
