#include "tape_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;

void TapeDelay::Init() {
    tape_line_.Init(tape_buf_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::SmoothRandom);
    lfo_.SetJitter(0.5f);
    filter_.Init();
    filter_.SetKnob(0.4f); // slight LP for tape warmth default
    sat_.Init(WaveCurve::Tape);
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_.Init();
    env_state_ = 0.0f;
    tape_lp_ = 0.0f;
}

void TapeDelay::Reset() {
    lfo_.Reset();
    tape_line_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_.Init();
    env_state_ = 0.0f;
    tape_lp_ = 0.0f;
    delay_smooth_ = -1.0f;
    aa_state_ = 0.0f;
    aa_coef_  = 1.0f;
    fb_lim_.Reset();
    pre_shelf_state_  = 0.0f;
    post_shelf_state_ = 0.0f;
}

void TapeDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_.SetKnob(params.filter);
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

    tape_line_.SetDelay(delay_samps);

    // Read Left tap (primary play head)
    float wet_l = tape_line_.Read();

    // Read Right tap (secondary play head, offset by 150 samples / ~3.1ms).
    // Linear interpolation is sufficient for a fixed decorrelation offset.
    float delay_r = delay_samps + kStereoOffsetSamples;
    if (delay_r > static_cast<float>(MAX_DELAY_SAMPLES - 1)) {
        delay_r = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    }
    float wet_r = tape_line_.ReadLinear(delay_r);

    // Feedback is mono-summed and processed through tape color & dynamic HF limiter
    float fb_mono = 0.5f * (wet_l + wet_r);

    // Pre-emphasis: boost HF before saturation (tape record EQ).
    // HP output = fb_mono - LP(fb_mono), scaled by grit.
    pre_shelf_state_ += shelf_coef_ * (fb_mono - pre_shelf_state_);
    const float pre_hp = fb_mono - pre_shelf_state_;
    fb_mono = fb_mono + shelf_gain_ * pre_hp;

    fb_mono = filter_.Process(fb_mono);
    fb_mono = sat_.Process(fb_mono);

    // De-emphasis: attenuate HF after saturation (tape reproduce EQ).
    // States are independent — pair cancels only approximately; residual HF boost
    // proportional to saturation nonlinearity is the intended tape character.
    post_shelf_state_ += shelf_coef_ * (fb_mono - post_shelf_state_);
    const float post_hp = fb_mono - post_shelf_state_;
    fb_mono = fb_mono - shelf_gain_ * post_hp;

    // Dynamic HF compression (tape HF demagnetization & saturation):
    // Envelope follower tracks feedback level
    const float env = (fb_mono > 0.0f) ? fb_mono : -fb_mono;
    env_state_ += 0.05f * (env - env_state_);

    // Lower cutoff coefficient (more low-pass) as volume/grit increases
    float k_lp = 0.45f - 0.35f * env_state_ * params.grit;
    if (k_lp < 0.05f) k_lp = 0.05f;
    if (k_lp > 0.45f) k_lp = 0.45f;

    tape_lp_ += k_lp * (fb_mono - tape_lp_);
    fb_mono = tape_lp_;

    // Apply gain compensation to keep loop gain controlled and prevent runaway feedback scream,
    // while still allowing warm, organic self-oscillation at maximum repeats/grit.
    const float comp = 1.0f / (1.0f + params.grit * params.grit * 14.0f);
    fb_mono *= comp;

    const float feedback = dc_fb_.Process(fb_lim_.Process(fb_mono * params.repeats));
    float write_val = input + feedback;
    // Hard stop: limiter guards feedback only; dry input can push write_val > 1.0.
    // ±2.0 prevents delay buffer runaway while preserving musical headroom.
    if (write_val >  2.0f) write_val =  2.0f;
    if (write_val < -2.0f) write_val = -2.0f;
    aa_state_ += aa_coef_ * (write_val - aa_state_);
    tape_line_.Write(aa_state_);

    // Apply DC blockers independently per channel
    wet_l = dc_l_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
