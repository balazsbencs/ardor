#pragma once

namespace pedal {

/// First-order allpass filter with modulatable coefficient.
/// Transfer function: H(z) = (a + z^-1) / (1 + a*z^-1)
/// Coefficient a in [-1, +1]; a=0 gives unity gain passthrough.
/// Used by PhaserMode to create phase-shifted copies for comb filtering.
class AllpassFilter {
public:
    void Reset() { state_ = 0.0f; }

    void SetCoeff(float a) { a_ = a; }
    float GetCoeff() const { return a_; }

    float Process(float input) {
        const float output = a_ * input + state_;
        state_ = input - a_ * output;
        return output;
    }

private:
    float a_     = 0.0f;  // allpass coefficient
    float state_ = 0.0f;  // unit delay state
};

} // namespace pedal
