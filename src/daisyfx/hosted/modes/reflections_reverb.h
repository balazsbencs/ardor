#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/early_reflections.h"
#include "../dsp/lfo.h"

namespace pedal {

class ReflectionsReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Reflections"; }

private:
    float            buf_pre_delay_l_[24000];
    float            buf_pre_delay_r_[24000];
    float            buf_er_l_[6144];
    float            buf_er_r_[6144];
    DelayLineSdram   pre_delay_l_;
    DelayLineSdram   pre_delay_r_;
    EarlyReflections er_l_;
    EarlyReflections er_r_;
    Lfo              motion_lfo_;
};

} // namespace pedal
