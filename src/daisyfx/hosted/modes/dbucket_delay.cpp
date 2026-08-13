#include "dbucket_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include <cstdint>
#include <cmath>

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 61.0f;

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
    // Tone remains independent of Drive. Drive now controls nonlinear input
    // gain, while adding only a restrained amount of BBD noise.
    float filter_knob = 0.5f + (params.filter - 0.5f) * 0.8f - params.grit * 0.08f;
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
    // Calibrated two-pole bandwidth: about 9 kHz at the shortest delay and
    // 2.5 kHz at the longest, logarithmically interpolated with clock time.
    const float cutoff = expf(logf(9000.0f) + t * (logf(2500.0f) - logf(9000.0f)));
    const float input_lp = 1.0f - expf(-6.2831853f * cutoff * INV_SAMPLE_RATE);
    bbd_l_.SetInputLpK(input_lp);
    bbd_r_.SetInputLpK(input_lp);
    bbd_l_.SetClockDelaySamples(ds);
    bbd_r_.SetClockDelaySamples(ds + kStereoOffsetSamples);
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
    const float modulation = params.mod_dep * 20.0f;
    const float delay_l = delay_smooth_ + lfo_val * modulation;
    const float delay_r = delay_smooth_ + kStereoOffsetSamples - lfo_val * modulation;
    const bool moving = modulation > 0.00001f || fabsf(base_samps - delay_smooth_) > 0.01f;
    const float tap_l = moving ? line_l_.ReadAtHighQuality(delay_l) : line_l_.ReadNearest(delay_l);
    const float tap_r = moving ? line_r_.ReadAtHighQuality(delay_r) : line_r_.ReadNearest(delay_r);
    float wet_l = filter_l_.Process(bbd_l_.Deemphasis(tap_l));
    float wet_r = filter_r_.Process(bbd_r_.Deemphasis(tap_r));

    const float feedback_l = dc_fb_l_.Process(wet_l * params.repeats);
    const float feedback_r = dc_fb_r_.Process(wet_r * params.repeats);
    line_l_.Write(bbd_l_.Process(input.left + feedback_l, params.grit, noise_seed_l_, delay_l));
    line_r_.Write(bbd_r_.Process(input.right + feedback_r, params.grit, noise_seed_r_, delay_r));

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
