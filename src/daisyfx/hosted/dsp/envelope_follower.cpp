#include "envelope_follower.h"
#include <cmath>

namespace pedal {

static float ms_to_coef(float ms, float sample_rate) {
    if (ms <= 0.0f) return 1.0f;
    return 1.0f - expf(-1.0f / (sample_rate * ms * 0.001f));
}

void EnvelopeFollower::Init(float attack_ms, float release_ms, float sample_rate) {
    sample_rate_ = (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : SAMPLE_RATE;
    SetAttack(attack_ms);
    SetRelease(release_ms);
    env_ = 0.0f;
}

void EnvelopeFollower::SetAttack(float ms) {
    attack_coef_ = ms_to_coef(ms, sample_rate_);
}

void EnvelopeFollower::SetRelease(float ms) {
    release_coef_ = ms_to_coef(ms, sample_rate_);
}

float EnvelopeFollower::Process(float sample) {
    float abs_in = sample < 0.0f ? -sample : sample;
    float coef   = abs_in > env_ ? attack_coef_ : release_coef_;
    env_ += coef * (abs_in - env_);
    return env_;
}

} // namespace pedal
