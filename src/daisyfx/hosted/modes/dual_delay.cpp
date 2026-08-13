#include "dual_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;
void DualDelay::Init() {
    line_l_.Init(buf_l_, kDualDelaySamples);
    line_r_.Init(buf_r_, kDualDelaySamples);
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
    time_transition_.Reset();
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void DualDelay::Prepare(const ParamSet& params) {
    time_transition_.SetTarget(params.time * SAMPLE_RATE);
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
    const auto readHeads = [&](float base) {
        const float mod_samps = base * params.mod_dep * 0.005f;
        const float delay_l = base + lfo_val * mod_samps;
        const float delay_r = base * (1.0f + 0.5f * pp) +
                              (1.0f - pp) * kStereoOffsetSamples - lfo_val * mod_samps;
        if (mod_samps <= 0.00001f) {
            return StereoFrame{line_l_.ReadNearest(delay_l), line_r_.ReadNearest(delay_r)};
        }
        return StereoFrame{line_l_.ReadAtHighQuality(delay_l),
                           line_r_.ReadAtHighQuality(delay_r)};
    };
    StereoFrame wet = readHeads(time_transition_.to());
    if (time_transition_.active()) {
        const StereoFrame old = readHeads(time_transition_.from());
        const float fade = time_transition_.mix();
        wet.left = old.left + fade * (wet.left - old.left);
        wet.right = old.right + fade * (wet.right - old.right);
        time_transition_.Advance();
    }
    float wet_l = wet.left;
    float wet_r = wet.right;

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    // Dynamic ping-pong crossfader based on grit (0.0 = parallel, 1.0 = full ping-pong)
    const float fb_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float fb_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));
    // At full ping-pong, fold the complete stereo input to the first head.
    // This keeps right-only material instead of silently discarding it.
    const float mono_input = 0.5f * (input.left + input.right);
    const float write_l = (1.0f - pp) * input.left + pp * mono_input +
                          (1.0f - pp) * fb_l + pp * fb_r;
    const float write_r = (1.0f - pp) * input.right +
                          (1.0f - pp) * fb_r + pp * fb_l;

    line_l_.Write(write_l);
    line_r_.Write(write_r);

    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
