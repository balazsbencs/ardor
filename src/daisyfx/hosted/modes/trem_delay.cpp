#include "trem_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

void TremDelay::Init() {
    trem_line_.Init(trem_buf_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_.Init();
    filter_.SetKnob(0.5f);
    dc_.Init();
}

void TremDelay::Reset() {
    trem_line_.Reset();
    dc_.Init();
    delay_smooth_ = 0.0f;
    fb_lim_.Reset();
}

void TremDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_.SetKnob(params.filter);
}

StereoFrame TremDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.001f;

    delay_smooth_ += kDelaySlew * (params.time * SAMPLE_RATE - delay_smooth_);
    trem_line_.SetDelay(delay_smooth_);

    const float lfo_val = lfo_.Process();
    const float gain    = 1.0f - params.mod_dep * (1.0f - lfo_val) * 0.5f;

    float wet = trem_line_.Read();
    wet = filter_.Process(wet);

    const float feedback = fb_lim_.Process(wet * params.repeats);
    trem_line_.Write(input + feedback);

    wet *= gain;
    wet = dc_.Process(wet);

    return StereoFrame{wet, wet};
}

} // namespace pedal
