#include "trem_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

void TremDelay::Init() {
    trem_line_l_.Init(trem_buf_l_, MAX_DELAY_SAMPLES);
    trem_line_r_.Init(trem_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    dc_l_.Init();
    dc_r_.Init();
}

void TremDelay::Reset() {
    trem_line_l_.Reset();
    trem_line_r_.Reset();
    lfo_.Reset();
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

void TremDelay::Prepare(const ParamSet& params) {
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

StereoFrame TremDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame TremDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val = lfo_.Process();
    const float sineTrem = (1.0f - lfo_val) * 0.5f;
    // Grit is exposed here as Shape: preserve the sine taper at zero, then
    // bias toward a more pulsed tremolo without introducing a hard edge.
    const float shapedTrem = sineTrem + params.grit * (sineTrem * sineTrem - sineTrem);
    const float gain = 1.0f - params.mod_dep * shapedTrem;

    float wet_l = trem_line_l_.ReadAt(delay_current_);
    float wet_r = trem_line_r_.ReadAt(delay_current_);
    if (time_crossfade_remaining_ > 0) {
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float previousWetL = trem_line_l_.ReadAt(delay_previous_);
        const float previousWetR = trem_line_r_.ReadAt(delay_previous_);
        wet_l = previousWetL + fade * (wet_l - previousWetL);
        wet_r = previousWetR + fade * (wet_r - previousWetR);
        --time_crossfade_remaining_;
    }
    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    const float feedback_l = fb_lim_l_.Process(wet_l * params.repeats);
    const float feedback_r = fb_lim_r_.Process(wet_r * params.repeats);
    trem_line_l_.Write(input.left + feedback_l);
    trem_line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l * gain), dc_r_.Process(wet_r * gain)};
}

} // namespace pedal
