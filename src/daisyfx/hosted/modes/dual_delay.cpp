#include "dual_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;
static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

void DualDelay::Init() {
    line_l_.Init(buf_l_, MAX_DELAY_SAMPLES);
    line_r_.Init(buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    dc_l_.Init();
    dc_r_.Init();
}

void DualDelay::Reset() {
    line_l_.Reset();
    line_r_.Reset();
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
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void DualDelay::Prepare(const ParamSet& params) {
    const float target_delay = params.time * SAMPLE_RATE;
    if (delay_current_ < 0.0f) {
        delay_current_ = target_delay;
        delay_previous_ = target_delay;
    } else if (fabsf(target_delay - delay_current_) > 0.01f) {
        delay_previous_ = delay_current_;
        delay_current_ = target_delay;
        time_crossfade_remaining_ = kTimeCrossfadeSamples;
    }
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
}

StereoFrame DualDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame DualDelay::Process(StereoFrame input, const ParamSet& params) {
    const float pp = params.grit;
    const float lfo_val   = lfo_.Process();
    const float mod_samps = delay_current_ * params.mod_dep * 0.005f;

    float delay_l = delay_current_ + lfo_val * mod_samps;
    float delay_r = delay_current_ * (1.0f + 0.5f * pp) +
                    (1.0f - pp) * kStereoOffsetSamples - lfo_val * mod_samps;

    if (delay_l < 1.0f) delay_l = 1.0f;
    if (delay_l > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
        delay_l = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    }

    if (delay_r < 1.0f) delay_r = 1.0f;
    if (delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
        delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    }

    line_l_.SetDelay(delay_l);
    line_r_.SetDelay(delay_r);

    float wet_l = line_l_.Read();
    float wet_r = line_r_.Read();

    if (time_crossfade_remaining_ > 0) {
        const float old_mod_samps = delay_previous_ * params.mod_dep * 0.005f;
        float old_delay_l = delay_previous_ + lfo_val * old_mod_samps;
        float old_delay_r = delay_previous_ * (1.0f + 0.5f * pp) +
                            (1.0f - pp) * kStereoOffsetSamples - lfo_val * old_mod_samps;
        if (old_delay_l < 2.0f) old_delay_l = 2.0f;
        if (old_delay_l > static_cast<float>(MAX_DELAY_SAMPLES - 3)) old_delay_l = static_cast<float>(MAX_DELAY_SAMPLES - 3);
        if (old_delay_r < 2.0f) old_delay_r = 2.0f;
        if (old_delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 3)) old_delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 3);
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float old_wet_l = line_l_.ReadAt(old_delay_l);
        const float old_wet_r = line_r_.ReadAt(old_delay_r);
        wet_l = old_wet_l + fade * (wet_l - old_wet_l);
        wet_r = old_wet_r + fade * (wet_r - old_wet_r);
        --time_crossfade_remaining_;
    }

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    // Dynamic ping-pong crossfader based on grit (0.0 = parallel, 1.0 = full ping-pong)
    const float fb_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float fb_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));
    const float write_l = input.left + (1.0f - pp) * fb_l + pp * fb_r;
    const float write_r = input.right * (1.0f - pp) + (1.0f - pp) * fb_r + pp * fb_l;

    line_l_.Write(write_l);
    line_r_.Write(write_r);

    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
