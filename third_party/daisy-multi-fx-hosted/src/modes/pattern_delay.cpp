#include "pattern_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

// Must define the constexpr static data member in exactly one TU
constexpr float PatternDelay::PATTERNS[3][3];

static float DSY_SDRAM_BSS pattern_buf[MAX_DELAY_SAMPLES];
static DelayLineSdram       pattern_line;

void PatternDelay::Init() {
    pattern_line.Init(pattern_buf, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_.Init();
    filter_.SetKnob(0.5f);
    dc_.Init();
}

void PatternDelay::Reset() {
    pattern_line.Reset();
    dc_.Init();
    dc_fb_.Init();
    delay_smooth_ = -1.0f;
}

void PatternDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_.SetKnob(params.filter);
}

StereoFrame PatternDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.001f;
    const float target_samps = params.time * SAMPLE_RATE;
    if (delay_smooth_ < 0.0f) delay_smooth_ = target_samps;
    const float lfo_val = lfo_.Process();
    {
        float step = kDelaySlew * (target_samps - delay_smooth_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_ += step;
    }
    float base_samps = delay_smooth_ + lfo_val * (params.mod_dep * 25.0f);
    if (base_samps < 1.0f) base_samps = 1.0f;

    // Select pattern: grit 0..0.333 -> 0, 0.333..0.667 -> 1, 0.667..1 -> 2
    int pat_idx = static_cast<int>(params.grit * 3.0f);
    if (pat_idx < 0) pat_idx = 0;
    if (pat_idx > 2) pat_idx = 2;

    // Cap base_samps so the largest tap stays within the delay buffer
    const float max_mult = PATTERNS[pat_idx][2];
    const float max_base = static_cast<float>(MAX_DELAY_SAMPLES - 3) / max_mult;
    if (base_samps > max_base) base_samps = max_base;

    // Sum three rhythmic taps; cache first tap for feedback
    float wet = 0.0f;
    float first_tap = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float tap_samps = base_samps * PATTERNS[pat_idx][i];
        if (tap_samps < 1.0f)
            tap_samps = 1.0f;
        if (tap_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
            tap_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);
        const float tap = pattern_line.ReadAt(tap_samps);
        if (i == 0) first_tap = tap;
        wet += tap;
    }
    wet *= 0.57735027f; // normalise 3-tap sum (1/√3 for equal-power)

    wet = filter_.Process(wet);

    const float feedback = dc_fb_.Process(first_tap * params.repeats);
    pattern_line.Write(input + feedback);

    wet = dc_.Process(wet);

    return StereoFrame{wet, wet};
}

} // namespace pedal
