#include "pattern_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

// Must define the constexpr static data member in exactly one TU
constexpr float PatternDelay::PATTERNS[3][3];

void PatternDelay::Init() {
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

void PatternDelay::Reset() {
    line_l_.Reset();
    line_r_.Reset();
    lfo_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
    delay_current_ = -1.0f;
    delay_previous_ = -1.0f;
    time_crossfade_remaining_ = 0;
}

void PatternDelay::Prepare(const ParamSet& params) {
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

StereoFrame PatternDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame PatternDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val = lfo_.Process();
    float base_samps = delay_current_ + lfo_val * (params.mod_dep * 25.0f);
    if (base_samps < 1.0f) base_samps = 1.0f;

    // Select pattern: grit 0..0.333 -> 0, 0.333..0.667 -> 1, 0.667..1 -> 2
    int pat_idx = static_cast<int>(params.grit * 3.0f);
    if (pat_idx < 0) pat_idx = 0;
    if (pat_idx > 2) pat_idx = 2;

    // Cap base_samps so the largest tap stays within the delay buffer
    const float max_mult = PATTERNS[pat_idx][2];
    const float max_base = static_cast<float>(MAX_DELAY_SAMPLES - 3) / max_mult;
    if (base_samps > max_base) base_samps = max_base;

    float previousBase = delay_previous_ + lfo_val * (params.mod_dep * 25.0f);
    if (previousBase < 1.0f) previousBase = 1.0f;
    if (previousBase > max_base) previousBase = max_base;
    const bool crossfading = time_crossfade_remaining_ > 0;
    const float fade = crossfading
        ? 1.0f - static_cast<float>(time_crossfade_remaining_) / static_cast<float>(kTimeCrossfadeSamples)
        : 1.0f;

    // Sum three rhythmic taps; cache first taps for the independent loops.
    float wet_l = 0.0f;
    float wet_r = 0.0f;
    float first_tap_l = 0.0f;
    float first_tap_r = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float tap_samps = base_samps * PATTERNS[pat_idx][i];
        if (tap_samps < 1.0f)
            tap_samps = 1.0f;
        if (tap_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
            tap_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);
        const float tap_l = line_l_.ReadAt(tap_samps);
        const float tap_r = line_r_.ReadAt(tap_samps);
        float blended_l = tap_l;
        float blended_r = tap_r;
        if (crossfading) {
            float previousTap = previousBase * PATTERNS[pat_idx][i];
            if (previousTap < 1.0f) previousTap = 1.0f;
            if (previousTap > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
                previousTap = static_cast<float>(MAX_DELAY_SAMPLES - 1);
            }
            const float old_l = line_l_.ReadAt(previousTap);
            const float old_r = line_r_.ReadAt(previousTap);
            blended_l = old_l + fade * (tap_l - old_l);
            blended_r = old_r + fade * (tap_r - old_r);
        }
        if (i == 0) {
            first_tap_l = blended_l;
            first_tap_r = blended_r;
        }
        wet_l += blended_l;
        wet_r += blended_r;
    }
    if (crossfading) --time_crossfade_remaining_;
    wet_l *= 0.57735027f; // normalise 3-tap sum (1/√3 for equal-power)
    wet_r *= 0.57735027f;

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    const float feedback_l = dc_fb_l_.Process(first_tap_l * params.repeats);
    const float feedback_r = dc_fb_r_.Process(first_tap_r * params.repeats);
    line_l_.Write(input.left + feedback_l);
    line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
