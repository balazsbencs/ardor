#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"
#include "../dsp/fdn.h"
#include "../dsp/pitch_shifter.h"
#include "../dsp/tone_filter.h"

namespace pedal {

class ShimmerReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Shimmer"; }
    void SetHold(bool h) override;
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram pre_delay_;
    Diffuser       diffuser_;
    Fdn            fdn_;
    PitchShifter   pitch_shifter_[2];
    ToneFilter     tone_[2];
    bool           hold_            = false;
    float          pitch_fb_l_      = 0.0f;  // one-sample-delayed shimmer feedback left
    float          pitch_fb_r_      = 0.0f;  // one-sample-delayed shimmer feedback right
};

} // namespace pedal
