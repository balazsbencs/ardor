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
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Spring"; }
    void SetHold(bool h) override;
    bool SupportsHold() const override { return true; }

private:
    float              s0_ap0_[171];
    float              s0_ap1_[231];
    float              s0_ap2_[311];
    float              s0_ap3_[431];
    float              s0_ap4_[591];
    float              s0_ap5_[795];
    float              s1_ap0_[183];
    float              s1_ap1_[248];
    float              s1_ap2_[333];
    float              s1_ap3_[462];
    float              s1_ap4_[633];
    float              s1_ap5_[850];
    float              s2_ap0_[193];
    float              s2_ap1_[261];
    float              s2_ap2_[351];
    float              s2_ap3_[487];
    float              s2_ap4_[668];
    float              s2_ap5_[898];
    float              s0_comb_[4001];
    float              s1_comb_[4281];
    float              s2_comb_[4521];

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
    int                active_springs_ = 1;
};

} // namespace pedal
