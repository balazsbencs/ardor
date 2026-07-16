#pragma once
#include "../config/constants.h"
#include "fast_math.h"
#include <cmath>

namespace pedal {

namespace detail {
// [vowel][band]: formant frequencies in Hz
// AAHH, OH, OO, EE, AY, AAHHOH, OOOHOH
constexpr float kFormants[7][5] = {
    { 800, 1150, 2800, 3500, 4950 },
    { 400,  750, 2400, 2675, 2950 },
    { 270,  530, 2140, 2950, 3900 },
    { 730, 2058, 2979, 3600, 4100 },
    { 570, 1670, 2700, 3290, 4200 },
    { 600,  950, 2600, 3100, 4000 },
    { 330,  640, 2280, 2800, 3400 },
};
} // namespace detail

// 5-band biquad bandpass formant filter bank for vowel shaping.
class FormantFilter {
public:
    static constexpr int BANDS   = 5;
    static constexpr int VOWELS  = 7;

    void Init(float sample_rate = SAMPLE_RATE) {
        sample_rate_ = sample_rate;
        for (auto& b : bands_) b = {};
        SetVowel(0);
        ComputeCoeffs();
    }

    void Reset() {
        for (auto& b : bands_) { b.w1 = 0.0f; b.w2 = 0.0f; }
    }

    void SetVowel(int index) {
        if (index < 0 || index >= VOWELS) index = 0;
        vowel_ = index;
    }

    // Q: Mild=2, Medium=5, High=10
    void SetResonance(float Q) { Q_ = Q; }

    void Prepare() { ComputeCoeffs(); }

    float Process(float input) {
        float out = 0.0f;
        for (int i = 0; i < BANDS; ++i) {
            auto& b  = bands_[i];
            // Direct Form II transposed
            const float w = input - b.a1 * b.w1 - b.a2 * b.w2;
            out          += b.b0 * (w - b.w2);  // b2 = -b0
            b.w2 = b.w1;
            b.w1 = w;
        }
        return out * (1.0f / BANDS);
    }

private:
    struct Biquad {
        float b0 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float w1 = 0.0f;
        float w2 = 0.0f;
    };

    void ComputeCoeffs() {
        for (int i = 0; i < BANDS; ++i) {
            const float f0    = detail::kFormants[vowel_][i];
            const float w0    = 2.0f * 3.14159265f * f0 / sample_rate_;
            const float sw0   = std::sin(w0);
            const float cw0   = std::cos(w0);
            const float alpha = sw0 / (2.0f * Q_);
            const float a0    = 1.0f + alpha;
            bands_[i].b0 = alpha / a0;
            bands_[i].a1 = -2.0f * cw0 / a0;
            bands_[i].a2 = (1.0f - alpha) / a0;
        }
    }

    Biquad bands_[BANDS]{};
    float  sample_rate_ = SAMPLE_RATE;
    int    vowel_       = 0;
    float  Q_           = 5.0f;
};

} // namespace pedal
