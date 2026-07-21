#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/bbd_emulator.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include <cstdint>

namespace pedal {

class DbucketDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Dbucket"; }

private:
    Lfo           lfo_;
    ToneFilter    filter_l_;
    ToneFilter    filter_r_;
    DcBlocker     dc_l_;
    DcBlocker     dc_r_;
    DcBlocker     dc_fb_l_;
    DcBlocker     dc_fb_r_;
    BbdEmulator   bbd_l_;
    BbdEmulator   bbd_r_;
    uint32_t      noise_seed_l_ = 12345u;
    uint32_t      noise_seed_r_ = 0x9e3779b9u;
    float         delay_smooth_ = -1.0f;
    float          buf_l_[MAX_DELAY_SAMPLES];
    float          buf_r_[MAX_DELAY_SAMPLES];
    DelayLineSdram line_l_;
    DelayLineSdram line_r_;
};

} // namespace pedal
