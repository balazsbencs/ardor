#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/fdn.h"
#include "../dsp/tone_filter.h"
#include "../dsp/formant_filter.h"

namespace pedal {

class ChoraleReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Chorale"; }
    void SetHold(bool h) override { fdn_.SetHold(h); }
    bool SupportsHold() const override { return true; }

private:
    DelayLineSdram pre_delay_;
    FormantFilter  formant_;
    Fdn            fdn_;
    ToneFilter     tone_[2];
};

} // namespace pedal
