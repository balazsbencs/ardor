#include "magneto_reverb.h"
#include "daisy_seed.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

static constexpr size_t kMainDelaySize = static_cast<size_t>(REVERB_SAMPLE_RATE * 1.5f) + 16;
static float DSY_SDRAM_BSS buf_main[kMainDelaySize];
static float DSY_SDRAM_BSS buf_diff0[Diffuser::kDelays[0] + 1];
static float DSY_SDRAM_BSS buf_diff1[Diffuser::kDelays[1] + 1];
static float DSY_SDRAM_BSS buf_diff2[Diffuser::kDelays[2] + 1];
static float DSY_SDRAM_BSS buf_diff3[Diffuser::kDelays[3] + 1];

} // namespace

void MagnetoReverb::Init() {
    delay_.Init(buf_main, kMainDelaySize);
    delay_.SetDelay(240.0f);

    float* diff_bufs[Diffuser::STAGES] = {
        buf_diff0, buf_diff1, buf_diff2, buf_diff3
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_.Init(diff_bufs, diff_sizes);
    diffuser_.SetDiffusion(0.6f);

    n_heads_ = 4;
    for (auto& h : head_delays_) h = 0.0f;
    fb_lp_ = 0.0f;
}

void MagnetoReverb::Reset() {
    delay_.Reset();
    diffuser_.Reset();
    fb_lp_ = 0.0f;
}

void MagnetoReverb::Prepare(const ParamSet& params) {
    n_heads_ = (params.param1 < 0.33f) ? 3
             : (params.param1 < 0.66f) ? 4
             : 6;

    // Period based on decay (200ms – 1.5s range)
    const float decay_clamped = params.decay < 0.2f ? 0.2f
                              : (params.decay > 1.5f ? 1.5f : params.decay);
    const float period = decay_clamped * REVERB_SAMPLE_RATE;

    if (params.param2 < 0.5f) {
        // Even spacing
        for (int i = 0; i < n_heads_; ++i) {
            head_delays_[i] = period * (float)(i + 1) / (float)n_heads_;
        }
    } else {
        // Golden-ratio uneven spacing
        static constexpr float kPhi = 0.618033988f;
        float d = period * kPhi;
        for (int i = 0; i < n_heads_; ++i) {
            head_delays_[i] = d < 1.0f ? 1.0f : d;
            d *= kPhi;
        }
    }

    diffuser_.SetDiffusion(0.4f + params.mod * 0.4f);
}

StereoFrame MagnetoReverb::Process(float input, const ParamSet& params) {
    // Read multi-tap outputs before writing (avoids feedback from current input)
    float left  = 0.0f;
    float right = 0.0f;
    float fb_sum = 0.0f;

    for (int i = 0; i < n_heads_; ++i) {
        const float tap = delay_.ReadLinear(head_delays_[i]);
        fb_sum += tap;
        if ((i & 1) == 0) left  += tap;
        else               right += tap;
    }

    // Diffuse the mono mix once, then blend with stereo direct taps.
    const float diffed = diffuser_.Process(0.5f * (left + right));
    left  = left  * 0.5f + diffed * 0.5f;
    right = right * 0.5f + diffed * 0.5f;

    // Scale L/R symmetrically if heads are unbalanced
    const float l_heads = (float)((n_heads_ + 1) / 2);
    const float r_heads = (float)(n_heads_ / 2);
    if (l_heads > 0.0f) left  *= 0.7f / l_heads;
    if (r_heads > 0.0f) right *= 0.7f / r_heads;

    // Write input + feedback into delay.
    // One-pole LP tames broadband brightness; cap keeps the loop stable.
    float fb = params.pre_delay;          // 0..0.95
    if (fb > 0.85f) fb = 0.85f;
    const float fb_in = fb_sum / static_cast<float>(n_heads_);
    fb_lp_ += 0.4f * (fb_in - fb_lp_);
    delay_.Write(input + fb * fb_lp_);

    return StereoFrame{left, right};
}

} // namespace pedal
