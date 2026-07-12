#include "fdn.h"
#include "fast_math.h"
#include "../config/constants.h"
#include <cmath>
#include <cstring>

namespace pedal {

// Per-line LFO rates (Hz) — slightly different to prevent synchronization.
static constexpr float kLfoRates[Fdn::MAX_LINES] = {
    0.500f, 0.631f, 0.794f, 1.000f, 1.260f, 1.587f, 2.000f, 2.520f
};

void Fdn::Init(const Config& cfg) {
    n_lines_     = cfg.n_lines < 1 ? 1 : (cfg.n_lines > MAX_LINES ? MAX_LINES : cfg.n_lines);
    sample_rate_ = cfg.sample_rate > 0.0f ? cfg.sample_rate : 48000.0f;

    for (int i = 0; i < n_lines_; ++i) {
        lines_[i].Init(cfg.bufs[i], cfg.delays[i]);
        const float max_delay = static_cast<float>(cfg.delays[i]) - 2.0f - MAX_MOD_DEPTH_SAMPLES;
        const float delay_samples = max_delay > 2.0f ? max_delay : 2.0f;
        lines_[i].SetDelay(delay_samples);
        delay_samples_[i]    = delay_samples;
        modulated_delay_[i]  = delay_samples_[i];
        delay_s_[i]          = delay_samples_[i] / sample_rate_;
        lp_state_[i]  = 0.0f;
        lfo_phase_[i] = static_cast<float>(i) / static_cast<float>(n_lines_);
        feedback_[i]  = 0.7f;  // reasonable default
        dc_[i].Init();
    }
}

void Fdn::Reset() {
    for (int i = 0; i < n_lines_; ++i) {
        lines_[i].Reset();
        lp_state_[i] = 0.0f;
        dc_[i].Init();
    }
}

void Fdn::SetDecay(float decay_s) {
    if (decay_s <= 0.0f) decay_s = 0.001f;
    for (int i = 0; i < n_lines_; ++i) {
        // g = 10^(-3 * delay_s / decay_s) = exp(-6.9078 * delay_s / decay_s)
        feedback_[i] = std::exp(-6.9078f * delay_s_[i] / decay_s);
        if (feedback_[i] > 0.9999f) feedback_[i] = 0.9999f;
    }
}

void Fdn::SetDamping(float damp) {
    damp_ = damp < 0.0f ? 0.0f : (damp > 1.0f ? 1.0f : damp);
}

void Fdn::SetDampFromRt60Ratio(float rt60_lf_s, float hf_ratio) {
    if (rt60_lf_s <= 0.0f) rt60_lf_s = 0.001f;
    if (hf_ratio  <  0.01f) hf_ratio = 0.01f;
    if (hf_ratio  >  1.0f)  hf_ratio = 1.0f;

    const float rt60_hf_s = rt60_lf_s * hf_ratio;

    // Use median line delay for the approximation.
    const int   mid       = n_lines_ / 2;
    const float delay_med = delay_s_[mid];

    const float g_lf = expf(-6.9078f * delay_med / rt60_lf_s);
    const float g_hf = expf(-6.9078f * delay_med / rt60_hf_s);

    // From LP filter Nyquist gain: g_nyquist = damp/(2-damp)
    // Solve: damp = 2*g_hf / (g_lf + g_hf)
    const float g_sum = g_lf + g_hf;
    damp_ = (g_sum > 0.001f) ? (2.0f * g_hf / g_sum) : 0.5f;
    if (damp_ < 0.01f) damp_ = 0.01f;
    if (damp_ > 1.0f)  damp_ = 1.0f;
}

void Fdn::SetModulation(float depth_samples) {
    // Cap depth to 8 samples (~5 cents at 2.52 Hz) to stay below audible flutter threshold.
    const float clamped = depth_samples < 0.0f ? 0.0f : depth_samples;
    mod_depth_ = clamped > MAX_MOD_DEPTH_SAMPLES ? MAX_MOD_DEPTH_SAMPLES : clamped;
}

void Fdn::SetHold(bool hold) {
    hold_ = hold;
}

void Fdn::PrepareBlock() {
    const float block_inc = static_cast<float>(BLOCK_SIZE) / sample_rate_;
    for (int i = 0; i < n_lines_; ++i) {
        lfo_phase_[i] += kLfoRates[i] * block_inc;
        if (lfo_phase_[i] >= 1.0f) lfo_phase_[i] -= 1.0f;
        float d = delay_samples_[i];
        if (mod_depth_ > 0.0f) {
            d += mod_depth_ * fast_sin(lfo_phase_[i] * 6.28318530718f);
            if (d < 1.0f) d = 1.0f;
        }
        modulated_delay_[i] = d;
    }
}

void Fdn::hadamard4(float v[4]) const {
    const float a = v[0], b = v[1], c = v[2], d = v[3];
    v[0] = (a + b + c + d) * 0.5f;
    v[1] = (a - b + c - d) * 0.5f;
    v[2] = (a + b - c - d) * 0.5f;
    v[3] = (a - b - c + d) * 0.5f;
}

void Fdn::hadamard8(float v[8]) const {
    constexpr float kInvSqrt2 = 0.70710678118f;
    float a[4] = { v[0], v[1], v[2], v[3] };
    float b[4] = { v[4], v[5], v[6], v[7] };
    hadamard4(a);
    hadamard4(b);
    for (int i = 0; i < 4; ++i) {
        v[i]     = (a[i] + b[i]) * kInvSqrt2;
        v[i + 4] = (a[i] - b[i]) * kInvSqrt2;
    }
}

StereoFrame Fdn::Process(float input) {
    float v[MAX_LINES]{};

    // Read from each delay line using block-rate modulated tap positions.
    for (int i = 0; i < n_lines_; ++i) {
        v[i] = lines_[i].ReadLinear(modulated_delay_[i]);
    }

    // One-pole LP damping & DC blocking in feedback path.
    for (int i = 0; i < n_lines_; ++i) {
        float raw_blocked = dc_[i].Process(v[i]);
        if (hold_) {
            lp_state_[i] = raw_blocked;  // bypass LP during hold; frozen pad stays bright
        } else {
            lp_state_[i] += damp_ * (raw_blocked - lp_state_[i]);
        }
        if (!std::isfinite(lp_state_[i])) {
            lp_state_[i] = 0.0f;
            dc_[i].Init();
        }
    }

    // Hadamard mixing.
    float mixed[MAX_LINES]{};
    std::memcpy(mixed, lp_state_, n_lines_ * sizeof(float));
    if (n_lines_ != 4 && n_lines_ != 8) {
        return StereoFrame{ mixed[0], mixed[0] };
    }
    if (n_lines_ == 8) {
        hadamard8(mixed);
    } else {
        hadamard4(mixed);
    }

    // Distribute input and write back with feedback.
    const float input_gain = 1.0f / static_cast<float>(n_lines_);
    const float in_scaled = hold_ ? 0.0f : input * input_gain;  // mute input during hold
    for (int i = 0; i < n_lines_; ++i) {
        const float fb = hold_ ? 1.0f : feedback_[i];
        lines_[i].Write(in_scaled + fb * mixed[i]);
    }

    // Stereo output: even lines → L, odd lines → R.
    float left = 0.0f, right = 0.0f;
    const float out_scale = 2.0f / static_cast<float>(n_lines_);
    for (int i = 0; i < n_lines_; i += 2) left  += v[i];
    for (int i = 1; i < n_lines_; i += 2) right += v[i];
    return StereoFrame{ left * out_scale, right * out_scale };
}

StereoFrame Fdn::Process(StereoFrame input) {
    float v[MAX_LINES]{};

    // Read from each delay line using block-rate modulated tap positions.
    for (int i = 0; i < n_lines_; ++i) {
        v[i] = lines_[i].ReadLinear(modulated_delay_[i]);
    }

    // One-pole LP damping & DC blocking in feedback path.
    for (int i = 0; i < n_lines_; ++i) {
        float raw_blocked = dc_[i].Process(v[i]);
        if (hold_) {
            lp_state_[i] = raw_blocked;  // bypass LP during hold; frozen pad stays bright
        } else {
            lp_state_[i] += damp_ * (raw_blocked - lp_state_[i]);
        }
        if (!std::isfinite(lp_state_[i])) {
            lp_state_[i] = 0.0f;
            dc_[i].Init();
        }
    }

    // Hadamard mixing.
    float mixed[MAX_LINES]{};
    std::memcpy(mixed, lp_state_, n_lines_ * sizeof(float));
    if (n_lines_ != 4 && n_lines_ != 8) {
        return StereoFrame{ mixed[0], mixed[0] };
    }
    if (n_lines_ == 8) {
        hadamard8(mixed);
    } else {
        hadamard4(mixed);
    }

    // Distribute input and write back with feedback.
    const float input_gain = 1.0f / static_cast<float>(n_lines_);
    for (int i = 0; i < n_lines_; ++i) {
        const float fb = hold_ ? 1.0f : feedback_[i];
        const float in_val = hold_ ? 0.0f : ((i % 2 == 0) ? input.left : input.right) * input_gain;
        lines_[i].Write(in_val + fb * mixed[i]);
    }

    // Stereo output: even lines → L, odd lines → R.
    float left = 0.0f, right = 0.0f;
    const float out_scale = 2.0f / static_cast<float>(n_lines_);
    for (int i = 0; i < n_lines_; i += 2) left  += v[i];
    for (int i = 1; i < n_lines_; i += 2) right += v[i];
    return StereoFrame{ left * out_scale, right * out_scale };
}

} // namespace pedal
