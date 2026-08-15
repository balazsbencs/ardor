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

/// Antiderivative of soft_clip_tanh, for the anti-aliased version below.
///
/// Inside the knee the curve is (1/9)(x + 24x/(x^2+3)), which integrates to
/// (1/9)(x^2/2 + 12 ln(x^2+3)). Outside it the curve is flat at +/-1, so the
/// integral is |x| plus whatever constant makes the two meet: 0.813172 at
/// |x| = 3, where the slopes already agree at 1.
inline float soft_clip_tanh_integral(float x) noexcept {
    const float ax = x < 0.0f ? -x : x;
    if (ax >= 3.0f) return ax + 0.813172f;
    const float x2 = x * x;
    return (0.5f * x2 + 12.0f * std::log(x2 + 3.0f)) * (1.0f / 9.0f);
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
        const float integral = soft_clip_tanh_integral(x);
        const float delta = x - previous_;
        // Across a segment too short to divide by, fall back to the midpoint of
        // the curve, which is what the average tends to anyway.
        const float out = (delta > 1.0e-5f || delta < -1.0e-5f)
            ? (integral - previous_integral_) / delta
            : soft_clip_tanh(0.5f * (x + previous_));
        previous_ = x;
        previous_integral_ = integral;
        return out;
    }

private:
    float previous_ = 0.0f;
    float previous_integral_ = soft_clip_tanh_integral(0.0f);
};

} // namespace pedal
