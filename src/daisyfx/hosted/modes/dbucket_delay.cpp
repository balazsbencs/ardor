#include "dbucket_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include <cstdint>
#include <cmath>

using namespace pedal::delay_fx;

namespace pedal {

void DbucketDelay::Init() {
    line_.Init(buf_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_.Init();
    filter_.SetKnob(0.4f);
    dc_.Init();
    dc_fb_.Init();
    bbd_.Reset();
    noise_seed_   = 12345u;
    delay_smooth_ = 0.0f;
}

void DbucketDelay::Reset() {
    line_.Reset();
    dc_.Init();
    dc_fb_.Init();
    bbd_.Reset();
    noise_seed_   = 12345u;
    delay_smooth_ = -1.0f;
}

void DbucketDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    const float filter_knob = 0.4f - params.grit * 0.3f;
    filter_.SetKnob(filter_knob);
    // Log-map delay time to BBD LP coefficient: shorter delay = brighter, longer = darker.
    // 2880 = 60 ms at 48 kHz (min), 120000 = 2.5 s at 48 kHz (max).
    static constexpr float kBbdSampMin = 2880.0f;
    static constexpr float kBbdSampMax = 120000.0f;
    const float ds = params.time * SAMPLE_RATE;
    const float t = (ds <= kBbdSampMin) ? 0.0f
                  : (ds >= kBbdSampMax) ? 1.0f
                  : logf(ds / kBbdSampMin) / logf(kBbdSampMax / kBbdSampMin);
    bbd_.SetInputLpK(0.45f - t * 0.35f);
}

StereoFrame DbucketDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.0001f;  // BBD clock change glides pitch

    const float base_samps = params.time * SAMPLE_RATE;
    if (delay_smooth_ < 0.0f) delay_smooth_ = base_samps;
    {
        float step = kDelaySlew * (base_samps - delay_smooth_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_ += step;
    }

    const float lfo_val   = lfo_.Process();
    float delay_samps     = delay_smooth_ + lfo_val * (params.mod_dep * 20.0f);
    if (delay_samps < 1.0f)
        delay_samps = 1.0f;
    if (delay_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    line_.SetDelay(delay_samps);

    float wet = line_.Read();
    wet = bbd_.Deemphasis(wet);
    wet = filter_.Process(wet);

    const float feedback = dc_fb_.Process(wet * params.repeats);
    float write_in = bbd_.Process(input + feedback, params.grit, noise_seed_, delay_samps);
    line_.Write(write_in);

    wet = dc_.Process(wet);
    return StereoFrame{wet, wet};
}

} // namespace pedal
