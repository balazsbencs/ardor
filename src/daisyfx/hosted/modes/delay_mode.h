#pragma once
#include <cmath>
#include "../audio/stereo_frame.h"
#include "../params/delay_param_set.h"

namespace pedal {

// The right-head offset a delay mode reads its stereo image from, scaled by
// Width. It glides over ~20 ms, so turning Width moves the head smoothly, and
// settles on the exact target, so a static head can keep its integer read.
class StereoSpread {
public:
    void Reset() { offset_ = -1.0f; }
    // Call once per sample with the full-width offset and the Width control.
    float Update(float full_offset, float width) {
        const float target = full_offset * width;
        if (offset_ < 0.0f) {
            offset_ = target;
        } else {
            offset_ += kSlew * (target - offset_);
            if (std::fabs(target - offset_) < 1.0e-3f) offset_ = target;
        }
        return offset_;
    }
    // True while the offset sits between samples and needs an interpolated read.
    bool Fractional() const { return offset_ != std::floor(offset_); }

private:
    static constexpr float kSlew = 0.001f;
    float offset_ = -1.0f;
};

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
