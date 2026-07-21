#include "duck_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

void DuckDelay::Init() {
    duck_line_l_.Init(duck_buf_l_, MAX_DELAY_SAMPLES);
    duck_line_r_.Init(duck_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    // Moderate attack, slower release for smooth ducking
    follower_.Init(10.0f, 150.0f);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    dc_l_.Init();
    dc_r_.Init();
}

void DuckDelay::Reset() {
    duck_line_l_.Reset();
    duck_line_r_.Reset();
    lfo_.Reset();
    follower_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    delay_current_ = -1.0f;
    delay_previous_ = -1.0f;
    time_crossfade_remaining_ = 0;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
}

void DuckDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    const float targetDelay = params.time * SAMPLE_RATE;
    if (delay_current_ < 0.0f) {
        delay_current_ = targetDelay;
        delay_previous_ = targetDelay;
    } else if (fabsf(targetDelay - delay_current_) > 0.01f) {
        delay_previous_ = delay_current_;
        delay_current_ = targetDelay;
        time_crossfade_remaining_ = kTimeCrossfadeSamples;
    }
}

StereoFrame DuckDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame DuckDelay::Process(StereoFrame input, const ParamSet& params) {
    static constexpr float kThresh    = 0.10f;

    const float lfo_val   = lfo_.Process();
    float delay_samps     = delay_current_ + lfo_val * (params.mod_dep * 15.0f);
    if (delay_samps < 1.0f)
        delay_samps = 1.0f;
    if (delay_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    // Soft-knee duck: below 0.5*thresh transparent, above 1.5*thresh fully ducked
    const float detector = fmaxf(fabsf(input.left), fabsf(input.right));
    const float env = follower_.Process(detector);
    float t = (env - kThresh * 0.5f) / kThresh;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t);          // smoothstep
    const float duck_amount = 1.0f - t * params.grit;

    float wet_l = duck_line_l_.ReadAt(delay_samps);
    float wet_r = duck_line_r_.ReadAt(delay_samps);
    if (time_crossfade_remaining_ > 0) {
        float previousDelay = delay_previous_ + lfo_val * (params.mod_dep * 15.0f);
        if (previousDelay < 1.0f) previousDelay = 1.0f;
        if (previousDelay > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
            previousDelay = static_cast<float>(MAX_DELAY_SAMPLES - 1);
        }
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float previousWetL = duck_line_l_.ReadAt(previousDelay);
        const float previousWetR = duck_line_r_.ReadAt(previousDelay);
        wet_l = previousWetL + fade * (wet_l - previousWetL);
        wet_r = previousWetR + fade * (wet_r - previousWetR);
        --time_crossfade_remaining_;
    }
    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    const float feedback_l = fb_lim_l_.Process(wet_l * params.repeats);
    const float feedback_r = fb_lim_r_.Process(wet_r * params.repeats);
    duck_line_l_.Write(input.left + feedback_l);
    duck_line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l * duck_amount), dc_r_.Process(wet_r * duck_amount)};
}

} // namespace pedal
