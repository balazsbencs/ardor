#pragma once
#include "reverb_mode.h"
#include "../dsp/allpass.h"
#include "../dsp/comb_filter.h"
#include "../dsp/lfo.h"
#include "../dsp/saturation.h"
#include "../dsp/tone_filter.h"

namespace pedal {

class SpringReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Spring"; }
    void SetHold(bool h) override;
    bool SupportsHold() const override { return true; }

private:
    // 3 springs, 6 allpass stages each
    DelayAllpassFilter ap_[3][6];
    CombFilter         comb_[3];
    Saturation         sat_;
    ToneFilter         tone_[2];
    Lfo                spring_lfo_[3];
    float              mod_depth_ = 0.0f;
    bool               hold_ = false;
    float              comb_fb_[3]{};
    float              comb_makeup_[3]{};
};

} // namespace pedal
