#pragma once
#include "delay_mode.h"
#include "../dsp/lfo.h"
#include "../dsp/tone_filter.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/bbd_emulator.h"
#include <cstdint>

namespace pedal {

class DbucketDelay : public DelayMode {
public:
    void Init()  override;
    void Reset() override;
    void Prepare(const delay_fx::ParamSet& params) override;
    StereoFrame Process(float input, const delay_fx::ParamSet& params) override;
    const char* Name() const override { return "Dbucket"; }

private:
    Lfo         lfo_;
    ToneFilter  filter_;
    DcBlocker   dc_;
    DcBlocker   dc_fb_;
    BbdEmulator bbd_;
    uint32_t    noise_seed_   = 12345u;
    float       delay_smooth_ = 0.0f;
};

} // namespace pedal
