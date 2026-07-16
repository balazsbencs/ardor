#include "filter_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/fast_math.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;

void FilterDelay::Init() {
    filter_line_l_.Init(filter_buf_l_, MAX_DELAY_SAMPLES);
    filter_line_r_.Init(filter_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    dc_l_.Init();
    dc_r_.Init();
    svf_l_.Reset();
    svf_r_.Reset();
}

void FilterDelay::Reset() {
    filter_line_l_.Reset();
    filter_line_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    svf_l_.Reset();
    svf_r_.Reset();
    delay_smooth_l_ = -1.0f;
    delay_smooth_r_ = -1.0f;
    target_delay_ = 0.0f;
    filter_fb_gain_ = 1.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void FilterDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);

    target_delay_ = params.time * SAMPLE_RATE;
    if (target_delay_ > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        target_delay_ = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    float q = 0.5f + params.filter * 14.5f;
    svf_l_.SetQ(q);
    svf_r_.SetQ(q);
    filter_fb_gain_ = 1.0f / q;
    if (filter_fb_gain_ > 1.0f) filter_fb_gain_ = 1.0f;

    // Precompute g bounds for the LFO sweep so Process() can use SetG() instead of
    // calling tanf() 96 times per block (once per sample for each channel).
    const float max_mod = params.mod_dep * 1500.0f;
    const float f_lo = (800.0f - max_mod < 10.0f)    ? 10.0f    : 800.0f - max_mod;
    const float f_hi = (800.0f + max_mod > 20000.0f) ? 20000.0f : 800.0f + max_mod;
    g_lo_ = tanf(3.14159265f * f_lo * INV_SAMPLE_RATE);
    g_hi_ = tanf(3.14159265f * f_hi * INV_SAMPLE_RATE);
}

StereoFrame FilterDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.001f;

    if (delay_smooth_l_ < 0.0f) { delay_smooth_l_ = target_delay_; delay_smooth_r_ = target_delay_ + kStereoOffsetSamples; }
    {
        float step = kDelaySlew * (target_delay_ - delay_smooth_l_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_l_ += step;
    }
    delay_smooth_r_ = delay_smooth_l_ + kStereoOffsetSamples;
    if (delay_smooth_r_ > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_smooth_r_ = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    filter_line_l_.SetDelay(delay_smooth_l_);
    filter_line_r_.SetDelay(delay_smooth_r_);

    // Advance LFO per-sample
    float lfo_val = lfo_.Process();

    // Out-of-phase cutoff modulation per-sample to eliminate zipper noise (Bug 9).
    // g bounds are precomputed in Prepare() so no tanf() call is needed here.
    const float t_l = (lfo_val + 1.0f) * 0.5f;   // [0, 1]: 0 = lowest, 1 = highest
    const float t_r = (1.0f - lfo_val) * 0.5f;   // out of phase
    svf_l_.SetG(g_lo_ + t_l * (g_hi_ - g_lo_));
    svf_r_.SetG(g_lo_ + t_r * (g_hi_ - g_lo_));

    float wet_l = filter_line_l_.Read();
    float wet_r = filter_line_r_.Read();

    // Process through the TPT Svf
    svf_l_.Process(wet_l);
    svf_r_.Process(wet_r);

    // Select filter mode based on grit: <0.33 = LP, <0.66 = BP, >=0.66 = HP
    if (params.grit < 0.33f) {
        wet_l = svf_l_.lp();
        wet_r = svf_r_.lp();
    } else if (params.grit < 0.66f) {
        wet_l = svf_l_.bp();
        wet_r = svf_r_.bp();
    } else {
        wet_l = svf_l_.hp();
        wet_r = svf_r_.hp();
    }

    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats * filter_fb_gain_));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats * filter_fb_gain_));

    filter_line_l_.Write(input + feedback_l);
    filter_line_r_.Write(input + feedback_r);

    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
