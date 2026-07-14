#pragma once
#include "reverb_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/early_reflections.h"

namespace pedal {

class ReflectionsReverb : public ReverbMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const reverb_fx::ParamSet& params) override;
    StereoFrame Process(float input, const reverb_fx::ParamSet& params) override;
    const char* Name() const override { return "Reflections"; }

private:
    DelayLineSdram   pre_delay_;
    EarlyReflections er_;
};

} // namespace pedal
