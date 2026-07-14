#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"

namespace pedal {

class PatternDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Pattern"; }

private:
    Lfo        lfo_;
    ToneFilter filter_;
    DcBlocker  dc_;
    DcBlocker  dc_fb_;
    float      delay_smooth_ = 0.0f;

    // Tap multipliers for each of the 3 pattern types (3 taps each)
    static constexpr float PATTERNS[3][3] = {
        {1.0f, 2.0f,    3.0f  },   // straight
        {1.0f, 1.5f,    3.0f  },   // dotted 8th
        {0.667f, 1.333f, 2.0f },   // triplet
    };
};

} // namespace pedal
