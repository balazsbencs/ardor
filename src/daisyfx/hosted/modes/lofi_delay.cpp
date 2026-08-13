#include "lofi_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 47.0f;

void LofiDelay::Init() {
    line_l_.Init(buf_l_, MAX_DELAY_SAMPLES);
    line_r_.Init(buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Triangle);
    dc_l_.Init();
    dc_r_.Init();
    held_sample_l_ = held_sample_r_ = 0.0f;
    sr_counter_   = 0.0f;
    bits_         = 16;
    bit_scale_    = 32768.0f;
    decimate_     = 1.0f;
    aa_lp_l_ = aa_lp_r_ = 0.0f;
    time_transition_.Reset();
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
    bit_scale_    = 32768.0f;
    decimate_     = 1.0f;
    aa_lp_l_ = aa_lp_r_ = 0.0f;
    time_transition_.Reset();
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
}

void LofiDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);

    // bits range: 16 (grit=0) down to 4 (grit=1)
    bits_ = 16 - static_cast<int>(params.grit * 12.0f);
    if (bits_ < 1) bits_ = 1;
    bit_scale_ = static_cast<float>(1 << (bits_ - 1));

    // grit=0: decimation factor=1 (passthrough), grit=1: factor=16
    decimate_ = 1.0f + params.grit * 15.0f;

    time_transition_.SetTarget(params.time * SAMPLE_RATE);
}

StereoFrame LofiDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame LofiDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val    = lfo_.Process();
    const float modulation = params.mod_dep * 20.0f;
    const auto readHeads = [&](float base) {
        const float left_delay = base + lfo_val * modulation;
        const float right_delay = base + kStereoOffsetSamples - lfo_val * modulation;
        if (modulation <= 0.00001f) {
            return StereoFrame{line_l_.ReadNearest(left_delay), line_r_.ReadNearest(right_delay)};
        }
        return StereoFrame{line_l_.ReadAtHighQuality(left_delay),
                           line_r_.ReadAtHighQuality(right_delay)};
    };
    StereoFrame wet = readHeads(time_transition_.to());
    if (time_transition_.active()) {
        const StereoFrame old = readHeads(time_transition_.from());
        const float fade = time_transition_.mix();
        wet.left = old.left + fade * (wet.left - old.left);
        wet.right = old.right + fade * (wet.right - old.right);
        time_transition_.Advance();
    }
    float wet_l = wet.left;
    float wet_r = wet.right;

    if (params.grit > 0.00001f) {
        // Anti-alias before sample-rate reduction. At Crush=0 the entire
        // degradation stage is a bit-exact bypass.
        float aa_k = 3.14159f / decimate_;
        aa_k *= 0.5f + params.filter;
        if (aa_k < 0.02f) aa_k = 0.02f;
        if (aa_k > 1.0f) aa_k = 1.0f;
        aa_lp_l_ += aa_k * (wet_l - aa_lp_l_);
        aa_lp_r_ += aa_k * (wet_r - aa_lp_r_);
        wet_l = aa_lp_l_;
        wet_r = aa_lp_r_;

        wet_l = roundf(wet_l * bit_scale_) / bit_scale_;
        wet_r = roundf(wet_r * bit_scale_) / bit_scale_;
        sr_counter_ += 1.0f;
        if (sr_counter_ >= decimate_) {
            sr_counter_ -= decimate_;
            held_sample_l_ = wet_l;
            held_sample_r_ = wet_r;
        }
        wet_l = held_sample_l_;
        wet_r = held_sample_r_;
    } else {
        aa_lp_l_ = held_sample_l_ = wet_l;
        aa_lp_r_ = held_sample_r_ = wet_r;
        sr_counter_ = 0.0f;
    }

    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));
    line_l_.Write(input.left + feedback_l);
    line_r_.Write(input.right + feedback_r);

    return StereoFrame{dc_l_.Process(wet_l), dc_r_.Process(wet_r)};
}

} // namespace pedal
