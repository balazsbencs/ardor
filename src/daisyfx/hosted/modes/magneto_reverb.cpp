#include "magneto_reverb.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

void MagnetoReverb::Init() {
    delay_l_.Init(buf_main_l_, kMainDelaySize);
    delay_r_.Init(buf_main_r_, kMainDelaySize);
    delay_l_.SetDelay(240.0f);
    delay_r_.SetDelay(240.0f);

    float* diff_bufs_l[Diffuser::STAGES] = {
        buf_diff_l0_, buf_diff_l1_, buf_diff_l2_, buf_diff_l3_
    };
    float* diff_bufs_r[Diffuser::STAGES] = {
        buf_diff_r0_, buf_diff_r1_, buf_diff_r2_, buf_diff_r3_
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_l_.Init(diff_bufs_l, diff_sizes);
    diffuser_r_.Init(diff_bufs_r, diff_sizes);
    diffuser_l_.SetDiffusion(0.6f);
    diffuser_r_.SetDiffusion(0.6f);

    n_heads_ = 4;
    golden_spacing_ = false;
    for (auto& h : head_delays_) h = 0.0f;
    fb_lp_l_ = 0.0f;
    fb_lp_r_ = 0.0f;
    golden_spacing_ = false;
}

void MagnetoReverb::Reset() {
    delay_l_.Reset();
    delay_r_.Reset();
    diffuser_l_.Reset();
    diffuser_r_.Reset();
    fb_lp_l_ = 0.0f;
    fb_lp_r_ = 0.0f;
}

void MagnetoReverb::Prepare(const ParamSet& params) {
    // Param1 is physical (1…6), not a normalized control. Select the three
    // supported head layouts from that physical range.
    const float head_setting = std::clamp(params.param1, 1.0f, 6.0f);
    if (n_heads_ == 3) {
        if (head_setting > 3.6f) n_heads_ = 4;
    } else if (n_heads_ == 4) {
        if (head_setting < 3.4f) n_heads_ = 3;
        else if (head_setting > 5.1f) n_heads_ = 6;
    } else if (head_setting < 4.9f) {
        n_heads_ = 4;
    }

    // Period based on decay (200ms – 1.5s range)
    const float decay_clamped = params.decay < 0.2f ? 0.2f
                              : (params.decay > 1.5f ? 1.5f : params.decay);
    const float period = decay_clamped * REVERB_SAMPLE_RATE;

    if (golden_spacing_) {
        if (params.param2 < 0.45f) golden_spacing_ = false;
    } else if (params.param2 > 0.55f) {
        golden_spacing_ = true;
    }

    if (!golden_spacing_) {
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

    diffuser_l_.SetDiffusion(0.4f + params.mod * 0.4f);
    diffuser_r_.SetDiffusion(0.4f + params.mod * 0.4f);
}

StereoFrame MagnetoReverb::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame MagnetoReverb::Process(StereoFrame input, const ParamSet& params) {
    // Read multi-tap outputs before writing (avoids feedback from current input)
    float l_even = 0.0f, l_odd = 0.0f;
    float r_even = 0.0f, r_odd = 0.0f;
    float fb_sum_l = 0.0f, fb_sum_r = 0.0f;

    for (int i = 0; i < n_heads_; ++i) {
        const float tap_l = delay_l_.ReadLinear(head_delays_[i]);
        const float tap_r = delay_r_.ReadLinear(head_delays_[i]);
        fb_sum_l += tap_l;
        fb_sum_r += tap_r;
        if ((i & 1) == 0) { l_even += tap_l; r_even += tap_r; }
        else              { l_odd  += tap_l; r_odd  += tap_r; }
    }

    // Mirror the right-input head field to retain the source stereo image.
    float left  = l_even + r_odd;
    float right = l_odd + r_even;
    left  = left  * 0.5f + diffuser_l_.Process(0.5f * (l_even + l_odd)) * 0.5f;
    right = right * 0.5f + diffuser_r_.Process(0.5f * (r_even + r_odd)) * 0.5f;

    // Scale L/R symmetrically if heads are unbalanced
    const float l_heads = (float)((n_heads_ + 1) / 2);
    const float r_heads = (float)(n_heads_ / 2);
    if (l_heads > 0.0f) left  *= 0.7f / l_heads;
    if (r_heads > 0.0f) right *= 0.7f / r_heads;

    // Write input + feedback into delay.
    // One-pole LP tames broadband brightness; cap keeps the loop stable.
    float fb = params.pre_delay;          // 0..0.95
    if (fb > 0.85f) fb = 0.85f;
    const float fb_in_l = fb_sum_l / static_cast<float>(n_heads_);
    const float fb_in_r = fb_sum_r / static_cast<float>(n_heads_);
    fb_lp_l_ += 0.4f * (fb_in_l - fb_lp_l_);
    fb_lp_r_ += 0.4f * (fb_in_r - fb_lp_r_);
    delay_l_.Write(input.left + fb * fb_lp_l_);
    delay_r_.Write(input.right + fb * fb_lp_r_);

    return StereoFrame{left, right};
}

} // namespace pedal
