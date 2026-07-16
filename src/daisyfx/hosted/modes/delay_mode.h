#pragma once
#include "../audio/stereo_frame.h"
#include "../params/delay_param_set.h"

namespace pedal {

class DelayMode {
public:
    virtual ~DelayMode() = default;
    virtual void Init()  = 0;
    virtual void Reset() = 0;
    virtual void Prepare(const delay_fx::ParamSet& params) { (void)params; }
    virtual StereoFrame Process(float input, const delay_fx::ParamSet& params) = 0;
    virtual const char* Name() const = 0;
};

} // namespace pedal
