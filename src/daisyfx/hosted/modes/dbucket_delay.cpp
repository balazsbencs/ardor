#include "dbucket_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include <cstdint>
#include <cmath>

using namespace pedal::delay_fx;

namespace pedal {

void DbucketDelay::Init() {
    line_l_.Init(buf_l_, MAX_DELAY_SAMPLES);
    line_r_.Init(buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.4f);
    filter_r_.SetKnob(0.4f);
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
    bbd_l_.Reset();
    bbd_r_.Reset();
    noise_seed_l_ = 12345u;
    noise_seed_r_ = 0x9e3779b9u;
    delay_smooth_ = -1.0f;
}

void DbucketDelay::Reset() {
    line_l_.Reset();
    line_r_.Reset();
    lfo_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
    bbd_l_.Reset();
    bbd_r_.Reset();
    noise_seed_l_ = 12345u;
    noise_seed_r_ = 0x9e3779b9u;
    delay_smooth_ = -1.0f;
}

void DbucketDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    // Preserve the original slightly dark BBD default at filter=0.5, while
    // exposing a useful dark-to-bright range. Drive still progressively
    // darkens repeats as an analog BBD would.
    float filter_knob = 0.4f + (params.filter - 0.5f) * 0.6f - params.grit * 0.3f;
    if (filter_knob < 0.0f) filter_knob = 0.0f;
    if (filter_knob > 1.0f) filter_knob = 1.0f;
    filter_l_.SetKnob(filter_knob);
    filter_r_.SetKnob(filter_knob);
    // Log-map delay time to BBD LP coefficient: shorter delay = brighter, longer = darker.
    // 2880 = 60 ms at 48 kHz (min), 120000 = 2.5 s at 48 kHz (max).
    static constexpr float kBbdSampMin = 2880.0f;
    static constexpr float kBbdSampMax = 120000.0f;
    const float ds = params.time * SAMPLE_RATE;
    const float t = (ds <= kBbdSampMin) ? 0.0f
                  : (ds >= kBbdSampMax) ? 1.0f
                  : logf(ds / kBbdSampMin) / logf(kBbdSampMax / kBbdSampMin);
    const float input_lp = 0.45f - t * 0.35f;
    bbd_l_.SetInputLpK(input_lp);
    bbd_r_.SetInputLpK(input_lp);
}

StereoFrame DbucketDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame DbucketDelay::Process(StereoFrame input, const ParamSet& params) {
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

    line_l_.SetDelay(delay_samps);
    line_r_.SetDelay(delay_samps);

    float wet_l = filter_l_.Process(bbd_l_.Deemphasis(line_l_.Read()));
    float wet_r = filter_r_.Process(bbd_r_.Deemphasis(line_r_.Read()));

    const float feedback_l = dc_fb_l_.Process(wet_l * params.repeats);
    const float feedback_r = dc_fb_r_.Process(wet_r * params.repeats);
    line_l_.Write(bbd_l_.Process(input.left + feedback_l, params.grit, noise_seed_l_, delay_samps));
    line_r_.Write(bbd_r_.Process(input.right + feedback_r, params.grit, noise_seed_r_, delay_samps));

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
