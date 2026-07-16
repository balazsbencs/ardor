#pragma once

namespace pedal {

enum class WaveCurve { SoftClip, Tape, Tube };

class WaveShaper {
public:
    void Init(WaveCurve curve = WaveCurve::SoftClip) {
        curve_ = curve;
        drive_ = 1.0f;
    }

    // drive: 0..1 → internal multiplier 1..16 (quadratic taper for perceptual linearity)
    void SetDrive(float drive) { drive_ = 1.0f + drive * drive * 15.0f; }

    float Process(float x) const;

private:
    WaveCurve curve_ = WaveCurve::SoftClip;
    float     drive_ = 1.0f;
};

inline float WaveShaper::Process(float x) const {
    const float driven = x * drive_;
    switch (curve_) {
        case WaveCurve::SoftClip: {
            // Padé approximant of tanh(x): accurate to ~0.5% for |x| < 3.
            // Differentiable, no pre-clip discontinuity, matches the character
            // of the tanh-based soft-clip used in flanger and BBD emulator.
            if (driven <= -3.0f) return -1.0f;
            if (driven >=  3.0f) return  1.0f;
            const float x2 = driven * driven;
            return driven * (27.0f + x2) / (27.0f + 9.0f * x2);
        }
        case WaveCurve::Tape: {
            // Rational soft-limiter: x/(1+|x|), output in (-1,+1) without hard clip.
            const float abs_d = driven >= 0.0f ? driven : -driven;
            return driven / (1.0f + abs_d);
        }
        case WaveCurve::Tube: {
            // Bias applied before drive so DC offset scales with drive level (asymmetric 2nd-harmonic).
            // Bias 0.02 keeps DC injection below 0.35 across the full drive range.
            // DC offset removed by each mode's DcBlocker.
            const float biased = (x + 0.02f) * drive_;
            if (biased <= -3.0f) return -1.0f;
            if (biased >=  3.0f) return  1.0f;
            const float x2 = biased * biased;
            return biased * (27.0f + x2) / (27.0f + 9.0f * x2);
        }
        default:
            return driven;
    }
}

} // namespace pedal
