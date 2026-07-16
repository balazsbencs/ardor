#pragma once
#include "../audio/stereo_frame.h"
#include "../params/mod_param_set.h"

namespace pedal {

class ModMode {
public:
    virtual ~ModMode() = default;
    virtual void Init()  = 0;
    virtual void Reset() = 0;
    virtual void Prepare(const mod_fx::ParamSet& params) { (void)params; }
    virtual StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) = 0;
    virtual const char* Name() const = 0;
};

} // namespace pedal
