#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/delay_tap_transition.h"
#include "../config/constants.h"

namespace pedal {

class PatternDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Pattern"; }

private:
    Lfo        lfo_;
    ToneFilter filter_l_;
    ToneFilter filter_r_;
    DcBlocker  dc_l_;
    DcBlocker  dc_r_;
    DcBlocker  dc_fb_l_;
    DcBlocker  dc_fb_r_;
    DelayTapTransition time_transition_;
    DelayTapTransition pattern_transition_;
    int             selected_pattern_ = -1;
    static constexpr size_t kPatternDelaySamples = static_cast<size_t>(SAMPLE_RATE * 7.51f);
    float           buf_l_[kPatternDelaySamples];
    float           buf_r_[kPatternDelaySamples];
    DelayLineSdram  line_l_;
    DelayLineSdram  line_r_;

    // Tap multipliers for each of the 3 pattern types (3 taps each)
    static constexpr float PATTERNS[3][3] = {
        {1.0f, 2.0f,    3.0f  },   // straight
        {1.0f, 1.5f,    3.0f  },   // dotted 8th
        {2.0f / 3.0f, 4.0f / 3.0f, 2.0f }, // triplet
    };
};

} // namespace pedal
