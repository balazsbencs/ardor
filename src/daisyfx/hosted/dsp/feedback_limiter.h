#pragma once

namespace pedal {

// Peak follower + soft-knee gain reduction for delay feedback paths.
// Replaces hard ±1.0 clip with a smooth limiter that blooms musically at
// high feedback settings rather than abruptly clamping.
class FeedbackLimiter {
public:
    void Reset() { env_ = 0.0f; }

    float Process(float x) {
        const float env_in = x > 0.0f ? x : -x;
        // Attack ~1ms, release ~200ms at 48kHz
        const float coef = (env_in > env_) ? kAttack : kRelease;
        env_ += coef * (env_in - env_);

        if (env_ <= kThreshold) return x;

        // Soft-knee: gain reduces smoothly above threshold
        const float over = env_ - kThreshold;
        const float gain = kThreshold / (kThreshold + over * kKnee);
        return x * gain;
    }

private:
    static constexpr float kAttack    = 0.0204f;   // 1 − exp(−1/48)
    static constexpr float kRelease   = 0.000104f; // 1 − exp(−1/9600)
    static constexpr float kThreshold = 0.707f;    // −3 dBFS
    static constexpr float kKnee      = 0.5f;      // softer knee = more bloom
    float env_ = 0.0f;
};

} // namespace pedal
