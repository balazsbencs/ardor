#include "pattern_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

// Must define the constexpr static data member in exactly one TU
constexpr float PatternDelay::PATTERNS[3][3];

void PatternDelay::Init() {
    line_l_.Init(buf_l_, kPatternDelaySamples);
    line_r_.Init(buf_r_, kPatternDelaySamples);
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
    time_transition_.Reset();
    pattern_transition_.Reset();
    selected_pattern_ = -1;
}

void PatternDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    time_transition_.SetTarget(params.time * SAMPLE_RATE);

    // Hysteresis prevents a noisy control from repeatedly changing all taps
    // at a pattern boundary. Actual changes use the same click-free queued
    // transition as delay-time changes.
    if (selected_pattern_ < 0) {
        selected_pattern_ = params.grit < 1.0f / 3.0f ? 0
                          : params.grit < 2.0f / 3.0f ? 1 : 2;
    } else if (selected_pattern_ == 0 && params.grit > 0.36f) {
        selected_pattern_ = 1;
    } else if (selected_pattern_ == 1 && params.grit < 0.30f) {
        selected_pattern_ = 0;
    } else if (selected_pattern_ == 1 && params.grit > 0.70f) {
        selected_pattern_ = 2;
    } else if (selected_pattern_ == 2 && params.grit < 0.63f) {
        selected_pattern_ = 1;
    }
    pattern_transition_.SetTarget(static_cast<float>(selected_pattern_));
}

StereoFrame PatternDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame PatternDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val = lfo_.Process();
    const float modulation = params.mod_dep * 25.0f;
    struct BankOutput { StereoFrame wet; StereoFrame first; };
    const auto renderBank = [&](float base, int pattern) {
        static constexpr float weights_l[3] = {0.775f, 0.560f, 0.300f};
        static constexpr float weights_r[3] = {0.300f, 0.560f, 0.775f};
        BankOutput output{};
        base += lfo_val * modulation;
        for (int i = 0; i < 3; ++i) {
            const float delay = base * PATTERNS[pattern][i];
            const float tap_l = modulation <= 0.00001f ? line_l_.ReadNearest(delay)
                                                       : line_l_.ReadAtHighQuality(delay);
            const float tap_r = modulation <= 0.00001f ? line_r_.ReadNearest(delay)
                                                       : line_r_.ReadAtHighQuality(delay);
            if (i == 0) output.first = StereoFrame{tap_l, tap_r};
            output.wet.left += tap_l * weights_l[i];
            output.wet.right += tap_r * weights_r[i];
        }
        return output;
    };
    const auto blendBanks = [](const BankOutput& from, const BankOutput& to, float mix) {
        BankOutput output;
        output.wet.left = from.wet.left + mix * (to.wet.left - from.wet.left);
        output.wet.right = from.wet.right + mix * (to.wet.right - from.wet.right);
        output.first.left = from.first.left + mix * (to.first.left - from.first.left);
        output.first.right = from.first.right + mix * (to.first.right - from.first.right);
        return output;
    };
    const auto renderAtTime = [&](float base, int pattern) {
        BankOutput output = renderBank(base, pattern);
        if (time_transition_.active()) {
            output = blendBanks(renderBank(time_transition_.from(), pattern), output,
                                time_transition_.mix());
        }
        return output;
    };

    const int current_pattern = static_cast<int>(pattern_transition_.to() + 0.5f);
    BankOutput output = renderAtTime(time_transition_.to(), current_pattern);
    if (pattern_transition_.active()) {
        const int old_pattern = static_cast<int>(pattern_transition_.from() + 0.5f);
        output = blendBanks(renderAtTime(time_transition_.to(), old_pattern), output,
                            pattern_transition_.mix());
    }
    if (time_transition_.active()) time_transition_.Advance();
    if (pattern_transition_.active()) pattern_transition_.Advance();

    float wet_l = output.wet.left;
    float wet_r = output.wet.right;

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    const float feedback_l = dc_fb_l_.Process(output.first.left * params.repeats);
    const float feedback_r = dc_fb_r_.Process(output.first.right * params.repeats);
    line_l_.Write(input.left + feedback_l);
    line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
