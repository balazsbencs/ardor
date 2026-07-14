#include "nonlinear_reverb.h"
#include "daisy_seed.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::reverb_fx;

namespace pedal {

namespace {

static float DSY_SDRAM_BSS buf_pre_delay[24000];
static float DSY_SDRAM_BSS buf_diff0[Diffuser::kDelays[0] + 1];
static float DSY_SDRAM_BSS buf_diff1[Diffuser::kDelays[1] + 1];
static float DSY_SDRAM_BSS buf_diff2[Diffuser::kDelays[2] + 1];
static float DSY_SDRAM_BSS buf_diff3[Diffuser::kDelays[3] + 1];
static float DSY_SDRAM_BSS buf_fdn0[1452];
static float DSY_SDRAM_BSS buf_fdn1[1748];
static float DSY_SDRAM_BSS buf_fdn2[2084];
static float DSY_SDRAM_BSS buf_fdn3[2412];

} // namespace

// Compute shape gain from shape_phase (0..1) and shape index
static float shape_gain(float phase, int shape) {
    switch (shape) {
    case 0: // Swoosh: fast attack, slow decay
        return phase < 0.1f ? (phase * 10.0f)
                            : std::exp(-2.0f * (phase - 0.1f));
    case 1: // Reverse: slow attack, sharp cut
        return phase < 0.9f ? (phase / 0.9f) : 0.0f;
    case 2: // Ramp: linear fade-out
        return 1.0f - phase;
    case 3: // Gate: on for 50%, then silent
        return phase < 0.5f ? 1.0f : 0.0f;
    case 4: // Gauss: bell curve
    {
        const float x = (phase - 0.5f) * 4.0f;
        return std::exp(-x * x * 0.5f);
    }
    case 5: // Bounce: decaying oscillation
        return std::fabs(std::cos(phase * 6.283185f * 2.0f)) *
               std::exp(-phase * 3.0f);
    default:
        return 1.0f;
    }
}

void NonlinearReverb::Init() {
    pre_delay_.Init(buf_pre_delay, 24000);
    pre_delay_.SetDelay(1.0f);

    float* diff_bufs[Diffuser::STAGES] = {
        buf_diff0, buf_diff1, buf_diff2, buf_diff3
    };
    const size_t diff_sizes[Diffuser::STAGES] = { Diffuser::kDelays[0] + 1, Diffuser::kDelays[1] + 1, Diffuser::kDelays[2] + 1, Diffuser::kDelays[3] + 1 };
    diffuser_.Init(diff_bufs, diff_sizes);
    diffuser_.SetDiffusion(0.6f);

    Fdn::Config fdn_cfg;
    fdn_cfg.n_lines     = 4;
    fdn_cfg.sample_rate = REVERB_SAMPLE_RATE;
    fdn_cfg.bufs[0]     = buf_fdn0;
    fdn_cfg.bufs[1]     = buf_fdn1;
    fdn_cfg.bufs[2]     = buf_fdn2;
    fdn_cfg.bufs[3]     = buf_fdn3;
    fdn_cfg.delays[0]   = 726;
    fdn_cfg.delays[1]   = 874;
    fdn_cfg.delays[2]   = 1042;
    fdn_cfg.delays[3]   = 1206;
    for (int i = 4; i < Fdn::MAX_LINES; ++i) {
        fdn_cfg.bufs[i]   = nullptr;
        fdn_cfg.delays[i] = 0;
    }
    fdn_.Init(fdn_cfg);
    fdn_.SetDecay(1.0f);
    fdn_.SetDamping(0.3f);

    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    input_env_.Init(2.0f, 140.0f);
    shape_phase_ = 0.0f;
    input_env_slow_ = 0.0f;
    shape_gain_smooth_ = 0.0f;
    decay_rate_  = 1.0f / REVERB_SAMPLE_RATE;
}

void NonlinearReverb::Reset() {
    pre_delay_.Reset();
    diffuser_.Reset();
    fdn_.Reset();
    tone_[0].Init(REVERB_SAMPLE_RATE);
    tone_[1].Init(REVERB_SAMPLE_RATE);
    input_env_.Init(2.0f, 140.0f);
    shape_phase_ = 0.0f;
    input_env_slow_ = 0.0f;
    shape_gain_smooth_ = 0.0f;
}

void NonlinearReverb::Prepare(const ParamSet& params) {
    const float delay_samples = params.pre_delay * REVERB_SAMPLE_RATE;
    pre_delay_.SetDelay(delay_samples < 1.0f ? 1.0f : delay_samples);
    fdn_.SetDecay(params.decay);
    fdn_.SetDamping(0.15f + params.tone * 0.35f);
    fdn_.SetModulation(params.mod * Fdn::MAX_MOD_DEPTH_SAMPLES);
    tone_[0].SetKnob(params.tone);
    tone_[1].SetKnob(params.tone);

    diffuser_.SetDiffusion(0.4f + params.param2 * 0.4f);

    const float decay = params.decay < 0.01f ? 0.01f : params.decay;
    decay_rate_ = 1.0f / (decay * REVERB_SAMPLE_RATE);
    fdn_.PrepareBlock();
}

StereoFrame NonlinearReverb::Process(float input, const ParamSet& params) {
    pre_delay_.Write(input);
    const float pre = pre_delay_.Read();

    const float diffused = diffuser_.Process(pre);

    const StereoFrame late = fdn_.Process(diffused);

    const float input_env = input_env_.Process(pre);
    const bool onset = input_env > 0.035f && input_env > input_env_slow_ + 0.025f;
    input_env_slow_ += 0.0015f * (input_env - input_env_slow_);
    if (onset) shape_phase_ = 0.0f;

    // Shape selection from param1
    const int shape = (params.param1 < 0.166f) ? 0
                    : (params.param1 < 0.333f) ? 1
                    : (params.param1 < 0.500f) ? 2
                    : (params.param1 < 0.666f) ? 3
                    : (params.param1 < 0.833f) ? 4
                    : 5;

    const float target_gain = (shape_phase_ >= 1.0f) ? 0.0f : shape_gain(shape_phase_, shape);
    static constexpr float kGainSlew = 1.0f / (0.002f * REVERB_SAMPLE_RATE);
    shape_gain_smooth_ += kGainSlew * (target_gain - shape_gain_smooth_);

    // Advance phase after input onset; clamp instead of free-running.
    shape_phase_ += decay_rate_;
    if (shape_phase_ > 1.0f) shape_phase_ = 1.0f;

    const StereoFrame out{
        tone_[0].Process(late.left  * shape_gain_smooth_),
        tone_[1].Process(late.right * shape_gain_smooth_)
    };
    return out;
}

} // namespace pedal
