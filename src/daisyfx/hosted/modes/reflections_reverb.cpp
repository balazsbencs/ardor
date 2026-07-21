#include "reflections_reverb.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

// 16 fixed tap delays
static constexpr uint16_t kTapDelays[16] = {
     334,  447,  555,  664,
     801,  937, 1072, 1209,
    1366, 1525, 1686, 1849,
    2014, 2181, 2350, 2521,
};

// Fixed tap gains (decreasing)
static constexpr float kTapGains[16] = {
    0.90f, 0.85f, 0.80f, 0.75f,
    0.70f, 0.65f, 0.60f, 0.55f,
    0.50f, 0.45f, 0.40f, 0.35f,
    0.30f, 0.25f, 0.20f, 0.15f,
};

} // namespace

void ReflectionsReverb::Init() {
    pre_delay_l_.Init(buf_pre_delay_l_, 24000);
    pre_delay_r_.Init(buf_pre_delay_r_, 24000);
    pre_delay_l_.SetDelay(1.0f);
    pre_delay_r_.SetDelay(1.0f);

    er_l_.Init(buf_er_l_, 6144);
    er_r_.Init(buf_er_r_, 6144);
    motion_lfo_.Init(0.1f, LfoWave::Sine);

    // Build default tap table
    ErTap taps_l[16];
    ErTap taps_r[16];
    for (int i = 0; i < 16; ++i) {
        taps_l[i].delay_samples = kTapDelays[i];
        taps_l[i].gain          = kTapGains[i];
        // Alternating pan: -1 for even, +1 for odd
        taps_l[i].pan = (i & 1) ? 0.6f : -0.6f;
        taps_r[i] = taps_l[i];
        taps_r[i].pan = -taps_l[i].pan;
    }
    er_l_.SetTaps(taps_l, 16);
    er_r_.SetTaps(taps_r, 16);
}

void ReflectionsReverb::Reset() {
    pre_delay_l_.Reset();
    pre_delay_r_.Reset();
    er_l_.Reset();
    er_r_.Reset();
    motion_lfo_.Reset();
}

void ReflectionsReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_l_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    pre_delay_r_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);

    // `mod` controls slow, shallow spatial movement. It is applied to the
    // precomputed pan table at the 24 kHz control rate, avoiding per-sample
    // trig in this otherwise very cheap early-reflections algorithm.
    motion_lfo_.SetRate(0.05f + params.mod * 0.45f);
    const float motion = motion_lfo_.PrepareBlock() * params.mod * 0.16f;

    // param2 (Loc X): shift stereo pan spread
    // param1 (Loc Y): front-back — scale gains slightly
    ErTap taps_l[16];
    ErTap taps_r[16];
    for (int i = 0; i < 16; ++i) {
        taps_l[i].delay_samples = kTapDelays[i];
        taps_l[i].gain          = kTapGains[i] * (0.6f + params.param1 * 0.4f) * params.decay;
        // Pan: alternating + shifted by param2
        const float base_pan  = (i & 1) ? 1.0f : -1.0f;
        const float pan_shift = (params.param2 - 0.5f) * 1.6f;
        const float tap_motion = motion * fast_sin(static_cast<float>(i) * 1.618034f);
        float pan = base_pan * (0.3f + params.param2 * 0.7f) + pan_shift * 0.2f + tap_motion;
        if (pan >  1.0f) pan =  1.0f;
        if (pan < -1.0f) pan = -1.0f;
        taps_l[i].pan = pan;
        taps_r[i] = taps_l[i];
        taps_r[i].pan = -pan;
    }
    er_l_.SetTaps(taps_l, 16);
    er_r_.SetTaps(taps_r, 16);
}

StereoFrame ReflectionsReverb::Process(float input, const ParamSet& /*params*/) {
    return Process(StereoFrame{input, input}, ParamSet{});
}

StereoFrame ReflectionsReverb::Process(StereoFrame input, const ParamSet& /*params*/) {
    pre_delay_l_.Write(input.left);
    pre_delay_r_.Write(input.right);
    const StereoFrame reflections_l = er_l_.Process(pre_delay_l_.Read());
    const StereoFrame reflections_r = er_r_.Process(pre_delay_r_.Read());
    return {
        0.5f * (reflections_l.left + reflections_r.left),
        0.5f * (reflections_l.right + reflections_r.right)
    };
}

} // namespace pedal
