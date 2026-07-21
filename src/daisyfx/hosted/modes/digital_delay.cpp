#include "digital_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;
static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

void DigitalDelay::Init() {
    digital_line_l_.Init(digital_buf_l_, MAX_DELAY_SAMPLES);
    digital_line_r_.Init(digital_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    sat_.Init(WaveCurve::Tape);
    dc_l_.Init();
    dc_r_.Init();
}

void DigitalDelay::Reset() {
    digital_line_l_.Reset();
    digital_line_r_.Reset();
    lfo_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    delay_current_ = -1.0f;
    delay_previous_ = -1.0f;
    time_crossfade_remaining_ = 0;
    aa_state_l_ = 0.0f;
    aa_state_r_ = 0.0f;
    aa_coef_    = 1.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void DigitalDelay::Prepare(const ParamSet& params) {
    const float target_delay = params.time * SAMPLE_RATE;
    if (delay_current_ < 0.0f) {
        delay_current_ = target_delay;
        delay_previous_ = target_delay;
    } else if (fabsf(target_delay - delay_current_) > 0.01f) {
        // Clean delays switch taps rather than varispeeding through a large
        // time change. Crossfading retains both tails without pitch smear.
        delay_previous_ = delay_current_;
        delay_current_ = target_delay;
        time_crossfade_remaining_ = kTimeCrossfadeSamples;
    }
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    sat_.SetDrive(params.grit);
    // Anti-alias LP: cutoff tracks mod depth × rate.
    // At zero mod this is transparent (coef=1). At max mod it rolls off ~8 kHz.
    const float mod_rate_hz = params.mod_spd * params.mod_dep * 30.0f;
    const float norm = mod_rate_hz / (10.0f * 30.0f);  // max speed × max depth_samples
    const float aa_fc = fmaxf(20000.0f - norm * 12000.0f, 100.0f);  // 20kHz → 8kHz, floor 100Hz
    aa_coef_ = 1.0f - expf(-2.0f * 3.14159265f * aa_fc * INV_SAMPLE_RATE);
}

StereoFrame DigitalDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame DigitalDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val   = lfo_.Process();
    const float mod_samps = params.mod_dep * 30.0f;

    float delay_l = delay_current_ + lfo_val * mod_samps;
    float delay_r = delay_current_ + kStereoOffsetSamples - lfo_val * mod_samps;

    if (delay_l < 1.0f) delay_l = 1.0f;
    if (delay_l > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_l = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    if (delay_r < 1.0f) delay_r = 1.0f;
    if (delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    digital_line_l_.SetDelay(delay_l);
    digital_line_r_.SetDelay(delay_r);

    float wet_l = digital_line_l_.Read();
    float wet_r = digital_line_r_.Read();

    if (time_crossfade_remaining_ > 0) {
        float old_delay_l = delay_previous_ + lfo_val * mod_samps;
        float old_delay_r = delay_previous_ + kStereoOffsetSamples - lfo_val * mod_samps;
        if (old_delay_l < 2.0f) old_delay_l = 2.0f;
        if (old_delay_l > static_cast<float>(MAX_DELAY_SAMPLES - 3)) old_delay_l = static_cast<float>(MAX_DELAY_SAMPLES - 3);
        if (old_delay_r < 2.0f) old_delay_r = 2.0f;
        if (old_delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 3)) old_delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 3);
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float old_wet_l = digital_line_l_.ReadAt(old_delay_l);
        const float old_wet_r = digital_line_r_.ReadAt(old_delay_r);
        wet_l = old_wet_l + fade * (wet_l - old_wet_l);
        wet_r = old_wet_r + fade * (wet_r - old_wet_r);
        --time_crossfade_remaining_;
    }

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    // Keep the clean digital repeat path exactly transparent at grit=0, then
    // progressively blend in tape-like loop saturation at higher values.
    const float colored_l = wet_l + params.grit * (sat_.Process(wet_l) - wet_l);
    const float colored_r = wet_r + params.grit * (sat_.Process(wet_r) - wet_r);
    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(colored_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(colored_r * params.repeats));

    // Anti-alias LP on write input
    aa_state_l_ += aa_coef_ * ((input.left + feedback_l) - aa_state_l_);
    aa_state_r_ += aa_coef_ * ((input.right + feedback_r) - aa_state_r_);
    digital_line_l_.Write(aa_state_l_);
    digital_line_r_.Write(aa_state_r_);

    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
