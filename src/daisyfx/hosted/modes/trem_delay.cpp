#include "trem_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

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
    time_transition_.Reset();
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
}

void TremDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    time_transition_.SetTarget(params.time * SAMPLE_RATE);
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
    const float inverseSineTrem = (1.0f + lfo_val) * 0.5f;
    const float inverseShapedTrem = inverseSineTrem +
        params.grit * (inverseSineTrem * inverseSineTrem - inverseSineTrem);
    const float gain_l = 1.0f - params.mod_dep * shapedTrem;
    const float gain_r = 1.0f - params.mod_dep * inverseShapedTrem;

    float wet_l = trem_line_l_.ReadNearest(time_transition_.to());
    float wet_r = trem_line_r_.ReadNearest(time_transition_.to());
    if (time_transition_.active()) {
        const float fade = time_transition_.mix();
        const float old_l = trem_line_l_.ReadNearest(time_transition_.from());
        const float old_r = trem_line_r_.ReadNearest(time_transition_.from());
        wet_l = old_l + fade * (wet_l - old_l);
        wet_r = old_r + fade * (wet_r - old_r);
        time_transition_.Advance();
    }
    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    const float feedback_l = fb_lim_l_.Process(wet_l * params.repeats);
    const float feedback_r = fb_lim_r_.Process(wet_r * params.repeats);
    trem_line_l_.Write(input.left + feedback_l);
    trem_line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l * gain_l), dc_r_.Process(wet_r * gain_r)};
}

} // namespace pedal
