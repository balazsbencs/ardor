#include "tape_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;

void TapeDelay::Init() {
    tape_line_l_.Init(tape_buf_l_, MAX_DELAY_SAMPLES);
    tape_line_r_.Init(tape_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::SmoothRandom);
    lfo_.SetJitter(0.5f);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.4f); // slight LP for tape warmth default
    filter_r_.SetKnob(0.4f);
    sat_.Init(WaveCurve::Tape);
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
    env_state_l_ = env_state_r_ = 0.0f;
    tape_lp_l_ = tape_lp_r_ = 0.0f;
}

void TapeDelay::Reset() {
    lfo_.Reset();
    tape_line_l_.Reset();
    tape_line_r_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
    env_state_l_ = env_state_r_ = 0.0f;
    tape_lp_l_ = tape_lp_r_ = 0.0f;
    delay_smooth_ = -1.0f;
    aa_state_l_ = aa_state_r_ = 0.0f;
    aa_coef_  = 1.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    pre_shelf_state_l_ = pre_shelf_state_r_ = 0.0f;
    post_shelf_state_l_ = post_shelf_state_r_ = 0.0f;
}

void TapeDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    sat_.SetDrive(params.grit);
    const float flutter = params.mod_dep * 50.0f;
    const float mod_rate_hz = params.mod_spd * flutter;
    const float norm = mod_rate_hz / (10.0f * 50.0f);
    const float aa_fc = fmaxf(20000.0f - norm * 12000.0f, 100.0f);
    aa_coef_ = 1.0f - expf(-2.0f * 3.14159265f * aa_fc * INV_SAMPLE_RATE);

    // HF shelf at ~3 kHz for tape record/reproduce EQ simulation.
    static constexpr float kShelfFc = 3000.0f;
    shelf_coef_ = 1.0f - expf(-2.0f * 3.14159265f * kShelfFc * INV_SAMPLE_RATE);
    shelf_gain_ = params.grit;  // 0 = flat, 1 = full tape EQ
}

StereoFrame TapeDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame TapeDelay::Process(StereoFrame input, const ParamSet& params) {
    static constexpr float kDelaySlew = 0.0001f;  // ~0.2s glide for tape varispeed warp

    const float target_samps = params.time * SAMPLE_RATE;
    if (delay_smooth_ < 0.0f) delay_smooth_ = target_samps;
    const float lfo_val = lfo_.Process();
    const float flutter = params.mod_dep * 50.0f;
    {
        float step = kDelaySlew * (target_samps - delay_smooth_);
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        delay_smooth_ += step;
    }
    float delay_samps = delay_smooth_ + lfo_val * flutter;
    if (delay_samps < 1.0f) delay_samps = 1.0f;
    if (delay_samps > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        delay_samps = static_cast<float>(MAX_DELAY_SAMPLES - 1);

    tape_line_l_.SetDelay(delay_samps);

    // Read Left tap (primary play head)
    float wet_l = tape_line_l_.Read();

    // Read Right tap (secondary play head, offset by 150 samples / ~3.1ms).
    // Linear interpolation is sufficient for a fixed decorrelation offset.
    float delay_r = delay_samps + kStereoOffsetSamples;
    if (delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
        delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    }
    tape_line_r_.SetDelay(delay_r);
    float wet_r = tape_line_r_.Read();

    // Keep each input and feedback history independent. The right head remains
    // offset for the original tape-style width, but anti-phase stereo no longer
    // cancels before it reaches the delay buffers.
    auto colorFeedback = [&](float wet, ToneFilter& filter, float& preShelf,
                             float& postShelf, float& envelope, float& tapeLp) {
        preShelf += shelf_coef_ * (wet - preShelf);
        float colored = wet + shelf_gain_ * (wet - preShelf);
        colored = sat_.Process(filter.Process(colored));
        postShelf += shelf_coef_ * (colored - postShelf);
        colored -= shelf_gain_ * (colored - postShelf);
        const float magnitude = colored >= 0.0f ? colored : -colored;
        envelope += 0.05f * (magnitude - envelope);
        float lp = 0.45f - 0.35f * envelope * params.grit;
        if (lp < 0.05f) lp = 0.05f;
        tapeLp += lp * (colored - tapeLp);
        return tapeLp / (1.0f + params.grit * params.grit * 14.0f);
    };

    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(
        colorFeedback(wet_l, filter_l_, pre_shelf_state_l_, post_shelf_state_l_, env_state_l_, tape_lp_l_) * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(
        colorFeedback(wet_r, filter_r_, pre_shelf_state_r_, post_shelf_state_r_, env_state_r_, tape_lp_r_) * params.repeats));
    auto write = [&](float dry, float feedback, float& state, DelayLineSdram& line) {
        float value = dry + feedback;
        if (value > 2.0f) value = 2.0f;
        if (value < -2.0f) value = -2.0f;
        state += aa_coef_ * (value - state);
        line.Write(state);
    };
    write(input.left, feedback_l, aa_state_l_, tape_line_l_);
    write(input.right, feedback_r, aa_state_r_, tape_line_r_);

    // Apply DC blockers independently per channel
    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
