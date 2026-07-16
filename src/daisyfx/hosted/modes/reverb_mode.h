#pragma once
#include "../audio/stereo_frame.h"
#include "../params/reverb_param_set.h"

namespace pedal {

class ReverbMode {
public:
    virtual ~ReverbMode() = default;
    virtual void Init()  = 0;
    virtual void Reset() = 0;
    virtual void Prepare(const reverb_fx::ParamSet& params) { (void)params; }
    virtual StereoFrame Process(float input, const reverb_fx::ParamSet& params) = 0;
    virtual const char* Name() const = 0;

    // Infinite sustain — supported by FDN-based modes.
    virtual void SetHold(bool hold) { (void)hold; }
    virtual bool SupportsHold() const { return false; }
};

} // namespace pedal
