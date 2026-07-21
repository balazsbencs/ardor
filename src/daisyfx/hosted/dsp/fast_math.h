#pragma once
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

} // namespace pedal
