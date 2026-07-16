#include "duck_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

void DuckDelay::Init() {
    duck_line_.Init(duck_buf_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    // Moderate attack, slower release for smooth ducking
    follower_.Init(10.0f, 150.0f);
    filter_.Init();
    filter_.SetKnob(0.5f);
    dc_.Init();
}

void DuckDelay::Reset() {
    duck_line_.Reset();
    dc_.Init();
    delay_smooth_ = -1.0f;
    fb_lim_.Reset();
}

void DuckDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_.SetKnob(params.filter);
}

StereoFrame DuckDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.001f;
    static constexpr float kThresh    = 0.10f;

    const float target_samps = params.time * SAMPLE_RATE;
    if (delay_smooth_ < 0.0f) delay_smooth_ = target_samps;
    {
        float step = kDelaySlew * (target_samps - delay_smooth_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_ += step;
    }
    const float lfo_val   = lfo_.Process();
    float delay_samps     = delay_smooth_ + lfo_val * (params.mod_dep * 15.0f);
    if (delay_samps < 1.0f)
        delay_samps = 1.0f;
    if (delay_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    duck_line_.SetDelay(delay_samps);

    // Soft-knee duck: below 0.5*thresh transparent, above 1.5*thresh fully ducked
    const float env = follower_.Process(input);
    float t = (env - kThresh * 0.5f) / kThresh;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t);          // smoothstep
    const float duck_amount = 1.0f - t * params.grit;

    float wet = duck_line_.Read();
    wet = filter_.Process(wet);

    const float feedback = fb_lim_.Process(wet * params.repeats);
    duck_line_.Write(input + feedback);

    wet *= duck_amount;
    wet = dc_.Process(wet);

    return StereoFrame{wet, wet};
}

} // namespace pedal
