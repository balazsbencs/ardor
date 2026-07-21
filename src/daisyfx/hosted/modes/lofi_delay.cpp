#include "lofi_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::delay_fx;

namespace pedal {

static constexpr int kTimeCrossfadeSamples = 2400; // 50 ms at 48 kHz

void LofiDelay::Init() {
    line_l_.Init(buf_l_, MAX_DELAY_SAMPLES);
    line_r_.Init(buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Triangle);
    dc_l_.Init();
    dc_r_.Init();
    held_sample_l_ = held_sample_r_ = 0.0f;
    sr_counter_   = 0.0f;
    bits_         = 16;
    bit_scale_    = 65536.0f;
    decimate_     = 1.0f;
    aa_lp_l_ = aa_lp_r_ = 0.0f;
    delay_current_ = -1.0f;
    delay_previous_ = -1.0f;
    time_crossfade_remaining_ = 0;
}

void LofiDelay::Reset() {
    line_l_.Reset();
    line_r_.Reset();
    lfo_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
    held_sample_l_ = held_sample_r_ = 0.0f;
    sr_counter_   = 0.0f;
    bits_         = 16;
    bit_scale_    = 65536.0f;
    decimate_     = 1.0f;
    aa_lp_l_ = aa_lp_r_ = 0.0f;
    delay_current_ = -1.0f;
    delay_previous_ = -1.0f;
    time_crossfade_remaining_ = 0;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
}

void LofiDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);

    // bits range: 16 (grit=0) down to 4 (grit=1)
    bits_ = 16 - static_cast<int>(params.grit * 12.0f);
    if (bits_ < 1) bits_ = 1;
    bit_scale_ = static_cast<float>(1 << bits_);

    // grit=0: decimation factor=1 (passthrough), grit=1: factor=16
    decimate_ = 1.0f + params.grit * 15.0f;

    const float targetDelay = params.time * SAMPLE_RATE;
    if (delay_current_ < 0.0f) {
        delay_current_ = targetDelay;
        delay_previous_ = targetDelay;
    } else if (fabsf(targetDelay - delay_current_) > 0.01f) {
        delay_previous_ = delay_current_;
        delay_current_ = targetDelay;
        time_crossfade_remaining_ = kTimeCrossfadeSamples;
    }
}

StereoFrame LofiDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame LofiDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val    = lfo_.Process();
    float delay_samps      = delay_current_ + lfo_val * (params.mod_dep * 20.0f);
    if (delay_samps < 1.0f)
        delay_samps = 1.0f;
    if (delay_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    float wet_l = line_l_.ReadAt(delay_samps);
    float wet_r = line_r_.ReadAt(delay_samps);
    if (time_crossfade_remaining_ > 0) {
        float previousDelay = delay_previous_ + lfo_val * (params.mod_dep * 20.0f);
        if (previousDelay < 1.0f) previousDelay = 1.0f;
        if (previousDelay > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
            previousDelay = static_cast<float>(MAX_DELAY_SAMPLES - 1);
        }
        const float fade = 1.0f - static_cast<float>(time_crossfade_remaining_) /
                                      static_cast<float>(kTimeCrossfadeSamples);
        const float previousWetL = line_l_.ReadAt(previousDelay);
        const float previousWetR = line_r_.ReadAt(previousDelay);
        wet_l = previousWetL + fade * (wet_l - previousWetL);
        wet_r = previousWetR + fade * (wet_r - previousWetR);
        --time_crossfade_remaining_;
    }

    // Anti-alias LP: cut above Nyquist of the decimated sample rate
    float aa_k = 3.14159f / decimate_;
    // Tone is neutral at 0.5. Dark settings lower the anti-alias cutoff;
    // bright settings restore presence without defeating the Nyquist guard.
    aa_k *= 0.5f + params.filter;
    if (aa_k < 0.02f) aa_k = 0.02f;
    if (aa_k > 0.5f) aa_k = 0.5f;
    aa_lp_l_ += aa_k * (wet_l - aa_lp_l_);
    aa_lp_r_ += aa_k * (wet_r - aa_lp_r_);
    wet_l = aa_lp_l_;
    wet_r = aa_lp_r_;

    // Bit crush
    if (bits_ < 16) {
        wet_l = roundf(wet_l * bit_scale_) / bit_scale_;
        wet_r = roundf(wet_r * bit_scale_) / bit_scale_;
    }

    // Sample-rate reduction
    sr_counter_ += 1.0f;
    if (sr_counter_ >= decimate_) {
        sr_counter_ -= decimate_;
        held_sample_l_ = wet_l;
        held_sample_r_ = wet_r;
    }
    wet_l = held_sample_l_;
    wet_r = held_sample_r_;

    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));
    line_l_.Write(input.left + feedback_l);
    line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
