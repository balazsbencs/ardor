#include "reflections_reverb.h"
#include "daisy_seed.h"
#include "../config/constants.h"

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

static float DSY_SDRAM_BSS buf_pre_delay[24000];
static float DSY_SDRAM_BSS buf_er[6144];

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
    pre_delay_.Init(buf_pre_delay, 24000);
    pre_delay_.SetDelay(1.0f);

    er_.Init(buf_er, 6144);

    // Build default tap table
    ErTap taps[16];
    for (int i = 0; i < 16; ++i) {
        taps[i].delay_samples = kTapDelays[i];
        taps[i].gain          = kTapGains[i];
        // Alternating pan: -1 for even, +1 for odd
        taps[i].pan = (i & 1) ? 0.6f : -0.6f;
    }
    er_.SetTaps(taps, 16);
}

void ReflectionsReverb::Reset() {
    pre_delay_.Reset();
    er_.Reset();
}

void ReflectionsReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);

    // param2 (Loc X): shift stereo pan spread
    // param1 (Loc Y): front-back — scale gains slightly
    ErTap taps[16];
    for (int i = 0; i < 16; ++i) {
        taps[i].delay_samples = kTapDelays[i];
        taps[i].gain          = kTapGains[i] * (0.6f + params.param1 * 0.4f) * params.decay;
        // Pan: alternating + shifted by param2
        const float base_pan  = (i & 1) ? 1.0f : -1.0f;
        const float pan_shift = (params.param2 - 0.5f) * 1.6f;
        float pan             = base_pan * (0.3f + params.param2 * 0.7f) + pan_shift * 0.2f;
        if (pan >  1.0f) pan =  1.0f;
        if (pan < -1.0f) pan = -1.0f;
        taps[i].pan = pan;
    }
    er_.SetTaps(taps, 16);
}

StereoFrame ReflectionsReverb::Process(float input, const ParamSet& /*params*/) {
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    return er_.Process(pre);
}

} // namespace pedal
