#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/diffuser.h"

namespace pedal {

class MagnetoReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Magneto"; }

private:
    DelayLineSdram delay_;
    Diffuser       diffuser_;
    int            n_heads_        = 4;
    float          head_delays_[6]{};
    float          fb_lp_ = 0.0f;
};

} // namespace pedal
