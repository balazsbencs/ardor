#include "duck_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 31.0f;

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
    time_transition_.Reset();
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void DuckDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    time_transition_.SetTarget(params.time * SAMPLE_RATE);
}

StereoFrame DuckDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame DuckDelay::Process(StereoFrame input, const ParamSet& params) {
    static constexpr float kThresh    = 0.10f;

    const float lfo_val   = lfo_.Process();
    const float modulation = params.mod_dep * 15.0f;

    // Soft-knee duck: below 0.5*thresh transparent, above 1.5*thresh fully ducked
    const float detector = fmaxf(fabsf(input.left), fabsf(input.right));
    const float env = follower_.Process(detector);
    float t = (env - kThresh * 0.5f) / kThresh;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t);          // smoothstep
    const float duck_amount = 1.0f - t * params.grit;

    const auto readHeads = [&](float base) {
        const float left = base + lfo_val * modulation;
        const float right = base + kStereoOffsetSamples - lfo_val * modulation;
        if (modulation <= 0.00001f) {
            return StereoFrame{duck_line_l_.ReadNearest(left), duck_line_r_.ReadNearest(right)};
        }
        return StereoFrame{duck_line_l_.ReadAtHighQuality(left),
                           duck_line_r_.ReadAtHighQuality(right)};
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

    // DC blocker in the feedback path, matching the other seven delay modes.
    // The ToneFilter in this loop has non-zero DC gain, so offset otherwise
    // accumulates across repeats and eats headroom.
    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));
    duck_line_l_.Write(input.left + feedback_l);
    duck_line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l * duck_amount), dc_r_.Process(wet_r * duck_amount)};
}

} // namespace pedal
