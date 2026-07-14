#pragma once
#include "delay_mode.h"
#include "../dsp/envelope_follower.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/feedback_limiter.h"

namespace pedal {

class SwellDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Swell"; }

private:
    enum class SwellState { Idle, Attack, Decay };

    EnvelopeFollower follower_;
    DcBlocker        dc_;
    FeedbackLimiter  fb_lim_;

    SwellState state_    = SwellState::Idle;
    float      env_gain_ = 0.0f;
    float      attack_rate_ = 0.0f;
    float      decay_rate_  = 0.0f;
    float      delay_smooth_ = 0.0f;
    bool       prev_above_threshold_ = false;

    static constexpr float TRIGGER_THRESHOLD = 0.05f;
};

} // namespace pedal
