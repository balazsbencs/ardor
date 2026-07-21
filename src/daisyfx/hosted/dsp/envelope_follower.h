#pragma once
#include "../config/constants.h"

namespace pedal {

class EnvelopeFollower {
public:
    void Init(float attack_ms = 5.0f, float release_ms = 50.0f,
              float sample_rate = SAMPLE_RATE);
    void SetAttack(float ms);
    void SetRelease(float ms);
    // Clear only the detector state; retain the configured time constants.
    void Reset() { env_ = 0.0f; }
    float Process(float sample); // returns 0..1 envelope

private:
    float attack_coef_  = 0.0f;
    float release_coef_ = 0.0f;
    float env_          = 0.0f;
    float sample_rate_  = SAMPLE_RATE;
};

} // namespace pedal
