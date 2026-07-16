#include "destroyer_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

void DestroyerMode::Init() {
    Reset();
}

void DestroyerMode::Reset() {
    svf_.Reset();
    dc_.Init();
    held_sample_  = 0.0f;
    decimate_acc_ = 0.0f;
    decimate_rate_= 1.0f;
    bits_         = 16;
    rand_         = 12345;
}

void DestroyerMode::Prepare(const ParamSet& params) {
    // Bitcrush: depth 0..1 → bits 16..1
    bits_ = 16 - static_cast<int>(params.depth * 15.0f);
    if (bits_ < 1) bits_ = 1;

    // Decimation rate: speed already in physical units (1×–48×) from SPEED_DESTROYER
    decimate_rate_ = params.speed;

    // Post-filter
    const float cutoff = 80.0f + params.tone * (SAMPLE_RATE * 0.45f - 80.0f);
    svf_.SetFreq(cutoff > 20000.0f ? 20000.0f : cutoff);
    svf_.SetQ(0.5f + params.p1 * 8.0f);
}

StereoFrame DestroyerMode::Process(StereoFrame input, const ParamSet& params) {
    // Sample-rate decimation: hold sample for decimate_rate_ samples
    decimate_acc_ += 1.0f;
    if (decimate_acc_ >= decimate_rate_) {
        decimate_acc_ -= decimate_rate_;
        held_sample_ = input.mono();
    }
    float x = held_sample_;

    // Bit crushing: quantize to `bits_` bits.
    // At 1-bit use sign quantizer; otherwise use 2^bits levels over [-1, +1].
    if (bits_ <= 1) {
        x = (x >= 0.0f) ? 1.0f : -1.0f;
    } else if (bits_ < 16) {
        const float levels = static_cast<float>(1 << (bits_ - 1));
        x = floorf(x * levels + 0.5f) / levels;
        if (x > 1.0f) x = 1.0f;
    }

    // Post-filter (always LP — canonical bitcrusher output)
    svf_.Process(x);
    float wet = svf_.lp();

    // Vinyl noise: P2 0..1 → noise blend amount
    if (params.p2 > 0.0f) {
        rand_       = lcg_next(rand_);
        const float noise = lcg_to_float(rand_) * params.p2 * 0.02f;
        wet += noise;
    }

    wet = dc_.Process(wet);
    return {wet, wet};
}

} // namespace pedal
