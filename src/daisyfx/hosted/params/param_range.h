#pragma once
#include <cmath>

namespace pedal {

struct ParamRange {
    float min;
    float max;
    // Curve: 0=linear, positive=log (boost lows), negative=exp (boost highs)
    // Exponent: curve>0 -> t^(curve+1), curve<0 -> t^(1/(1-curve))
    float curve;
};

// Apply curve transform to normalized [0,1] value
inline float apply_curve(float t, float curve) {
    if (curve == 0.0f) return t;
    // curve > 0: more resolution at low end (exponent > 1)
    // curve < 0: more resolution at high end (exponent < 1)
    float exp;
    if (curve > 0.0f) exp = curve + 1.0f;
    else              exp = 1.0f / (1.0f - curve);
    // Fast paths for the two most common cases
    if (exp == 2.0f)  return t * t;
    if (exp == 0.5f)  return sqrtf(t);
    // General case — hardware FPU makes powf cheap on Cortex-M7
    return powf(t, exp);
}

inline float map_param(float normalized, const ParamRange& range) {
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    float curved = apply_curve(normalized, range.curve);
    return range.min + curved * (range.max - range.min);
}

// Inverse of apply_curve
inline float invert_curve(float curved, float curve) {
    if (curve == 0.0f) return curved;
    float exp;
    if (curve > 0.0f) exp = curve + 1.0f;
    else              exp = 1.0f / (1.0f - curve);
    return powf(curved, 1.0f / exp);
}

// Inverse of map_param: physical value → normalized [0,1]
inline float unmap_param(float value, const ParamRange& range) {
    float t = (value - range.min) / (range.max - range.min);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return invert_curve(t, range.curve);
}

} // namespace pedal
