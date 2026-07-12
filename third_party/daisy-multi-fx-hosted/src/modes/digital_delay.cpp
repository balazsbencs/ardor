#include "digital_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;

static float DSY_SDRAM_BSS digital_buf_l[MAX_DELAY_SAMPLES];
static float DSY_SDRAM_BSS digital_buf_r[MAX_DELAY_SAMPLES];
static DelayLineSdram       digital_line_l;
static DelayLineSdram       digital_line_r;

void DigitalDelay::Init() {
    digital_line_l.Init(digital_buf_l, MAX_DELAY_SAMPLES);
    digital_line_r.Init(digital_buf_r, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    dc_l_.Init();
    dc_r_.Init();
}

void DigitalDelay::Reset() {
    digital_line_l.Reset();
    digital_line_r.Reset();
    dc_l_.Init();
    dc_r_.Init();
    delay_smooth_l_ = -1.0f;
    delay_smooth_r_ = -1.0f;
    aa_state_l_ = 0.0f;
    aa_state_r_ = 0.0f;
    aa_coef_    = 1.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void DigitalDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    // Anti-alias LP: cutoff tracks mod depth × rate.
    // At zero mod this is transparent (coef=1). At max mod it rolls off ~8 kHz.
    const float mod_rate_hz = params.mod_spd * params.mod_dep * 30.0f;
    const float norm = mod_rate_hz / (10.0f * 30.0f);  // max speed × max depth_samples
    const float aa_fc = fmaxf(20000.0f - norm * 12000.0f, 100.0f);  // 20kHz → 8kHz, floor 100Hz
    aa_coef_ = 1.0f - expf(-2.0f * 3.14159265f * aa_fc * INV_SAMPLE_RATE);
}

StereoFrame DigitalDelay::Process(float input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.001f;

    const float base_samps = params.time * SAMPLE_RATE;
    if (delay_smooth_l_ < 0.0f) delay_smooth_l_ = base_samps;
    if (delay_smooth_r_ < 0.0f) delay_smooth_r_ = base_samps;
    {
        float step = kDelaySlew * (base_samps - delay_smooth_l_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_l_ += step;
    }
    {
        float step = kDelaySlew * (base_samps - delay_smooth_r_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_r_ += step;
    }

    const float lfo_val   = lfo_.Process();
    const float mod_samps = params.mod_dep * 30.0f;

    float delay_l = delay_smooth_l_ + lfo_val * mod_samps;
    float delay_r = delay_smooth_r_ + kStereoOffsetSamples - lfo_val * mod_samps;

    if (delay_l < 1.0f) delay_l = 1.0f;
    if (delay_l > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_l = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    if (delay_r < 1.0f) delay_r = 1.0f;
    if (delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    digital_line_l.SetDelay(delay_l);
    digital_line_r.SetDelay(delay_r);

    float wet_l = digital_line_l.Read();
    float wet_r = digital_line_r.Read();

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));

    // Anti-alias LP on write input
    aa_state_l_ += aa_coef_ * ((input + feedback_l) - aa_state_l_);
    aa_state_r_ += aa_coef_ * ((input + feedback_r) - aa_state_r_);
    digital_line_l.Write(aa_state_l_);
    digital_line_r.Write(aa_state_r_);

    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
