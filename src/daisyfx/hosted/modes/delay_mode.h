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
    // Existing mono modes retain their character; stereo-aware modes override
    // this without forcing an extra buffer allocation on every effect.
    virtual StereoFrame Process(StereoFrame input, const delay_fx::ParamSet& params) {
        return Process((input.left + input.right) * 0.5f, params);
    }
    virtual const char* Name() const = 0;
};

} // namespace pedal
