#pragma once
#include "reverb_mode.h"
#include "../config/constants.h"
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
    static constexpr size_t kMainDelaySize = static_cast<size_t>(REVERB_SAMPLE_RATE * 1.5f) + 16;

    DelayLineSdram delay_;
    Diffuser       diffuser_;
    int            n_heads_        = 4;
    float          head_delays_[6]{};
    float          fb_lp_ = 0.0f;

    float buf_main_[kMainDelaySize];
    float buf_diff0_[Diffuser::kDelays[0] + 1];
    float buf_diff1_[Diffuser::kDelays[1] + 1];
    float buf_diff2_[Diffuser::kDelays[2] + 1];
    float buf_diff3_[Diffuser::kDelays[3] + 1];
};

} // namespace pedal
