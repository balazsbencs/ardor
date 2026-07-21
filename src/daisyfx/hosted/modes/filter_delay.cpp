#include "filter_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/fast_math.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;
static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

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
    lfo_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    svf_l_.Reset();
    svf_r_.Reset();
    delay_current_ = -1.0f;
    delay_previous_ = -1.0f;
    time_crossfade_remaining_ = 0;
    filter_type_ = FilterType::Lowpass;
    filter_fb_gain_ = 1.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void FilterDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);

    float target_delay = params.time * SAMPLE_RATE;
    if (target_delay > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        target_delay = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    if (delay_current_ < 0.0f) {
        delay_current_ = target_delay;
        delay_previous_ = target_delay;
    } else if (fabsf(target_delay - delay_current_) > 0.01f) {
        delay_previous_ = delay_current_;
        delay_current_ = target_delay;
        time_crossfade_remaining_ = kTimeCrossfadeSamples;
    }

    float q = 0.5f + params.filter * 14.5f;
    svf_l_.SetQ(q);
    svf_r_.SetQ(q);
    filter_fb_gain_ = 1.0f / q;
    if (filter_fb_gain_ > 1.0f) filter_fb_gain_ = 1.0f;

    // Do not chatter between filter topologies when an automated control
    // hovers around a boundary. The 0.03 dead bands are inaudible in normal
    // use but eliminate rapid state changes and their associated clicks.
    switch (filter_type_) {
        case FilterType::Lowpass:
            if (params.grit > 0.36f) filter_type_ = FilterType::Bandpass;
            break;
        case FilterType::Bandpass:
            if (params.grit < 0.30f) filter_type_ = FilterType::Lowpass;
            else if (params.grit > 0.69f) filter_type_ = FilterType::Highpass;
            break;
        case FilterType::Highpass:
            if (params.grit < 0.63f) filter_type_ = FilterType::Bandpass;
            break;
    }

    // Precompute g bounds for the LFO sweep so Process() can use SetG() instead of
    // calling tanf() 96 times per block (once per sample for each channel).
    const float max_mod = params.mod_dep * 1500.0f;
    const float f_lo = (800.0f - max_mod < 10.0f)    ? 10.0f    : 800.0f - max_mod;
    const float f_hi = (800.0f + max_mod > 20000.0f) ? 20000.0f : 800.0f + max_mod;
    g_lo_ = tanf(3.14159265f * f_lo * INV_SAMPLE_RATE);
    g_hi_ = tanf(3.14159265f * f_hi * INV_SAMPLE_RATE);
}

StereoFrame FilterDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame FilterDelay::Process(StereoFrame input, const ParamSet& params) {
    float delay_l = delay_current_;
    float delay_r = delay_current_ + kStereoOffsetSamples;
    if (delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 1)) delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    filter_line_l_.SetDelay(delay_l);
    filter_line_r_.SetDelay(delay_r);

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

    if (time_crossfade_remaining_ > 0) {
        float old_delay_l = delay_previous_;
        float old_delay_r = delay_previous_ + kStereoOffsetSamples;
        if (old_delay_l < 2.0f) old_delay_l = 2.0f;
        if (old_delay_l > static_cast<float>(MAX_DELAY_SAMPLES - 3)) old_delay_l = static_cast<float>(MAX_DELAY_SAMPLES - 3);
        if (old_delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 3)) old_delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 3);
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float old_wet_l = filter_line_l_.ReadAt(old_delay_l);
        const float old_wet_r = filter_line_r_.ReadAt(old_delay_r);
        wet_l = old_wet_l + fade * (wet_l - old_wet_l);
        wet_r = old_wet_r + fade * (wet_r - old_wet_r);
        --time_crossfade_remaining_;
    }

    // Process through the TPT Svf
    svf_l_.Process(wet_l);
    svf_r_.Process(wet_r);

    switch (filter_type_) {
        case FilterType::Lowpass:
            wet_l = svf_l_.lp();
            wet_r = svf_r_.lp();
            break;
        case FilterType::Bandpass:
            wet_l = svf_l_.bp();
            wet_r = svf_r_.bp();
            break;
        case FilterType::Highpass:
            wet_l = svf_l_.hp();
            wet_r = svf_r_.hp();
            break;
    }

    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats * filter_fb_gain_));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats * filter_fb_gain_));

    filter_line_l_.Write(input.left + feedback_l);
    filter_line_r_.Write(input.right + feedback_r);

    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
