#include "tape_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"

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
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
    dc_fb_r_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
    sat_l_.Reset();
    sat_r_.Reset();
    env_state_l_ = env_state_r_ = 0.0f;
    tape_lp_l_ = tape_lp_r_ = 0.0f;
}

void TapeDelay::Reset() {
    spread_.Reset();
    lfo_.Reset();
    tape_line_l_.Reset();
    tape_line_r_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
    dc_fb_r_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
    sat_l_.Reset();
    sat_r_.Reset();
    env_state_l_ = env_state_r_ = 0.0f;
    tape_lp_l_ = tape_lp_r_ = 0.0f;
    delay_smooth_ = -1.0f;
    previous_lfo_ = 0.0f;
    aa_state_l_ = aa_state_r_ = 0.0f;
    aa_coef_  = 1.0f;
    aa_mix_   = 0.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    pre_shelf_state_l_ = pre_shelf_state_r_ = 0.0f;
    post_shelf_state_l_ = post_shelf_state_r_ = 0.0f;
}

void TapeDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    if (params.grit <= 0.00001f) { sat_l_.Reset(); sat_r_.Reset(); }
    const float flutter = params.mod_dep * 50.0f;
    if (flutter <= 0.00001f || params.mod_spd <= 0.00001f) {
        aa_coef_ = 1.0f;
        aa_mix_ = 0.0f;
    } else {
        const float mod_rate_hz = params.mod_spd * flutter;
        const float norm = mod_rate_hz / (10.0f * 50.0f);
        aa_mix_ = fminf(norm, 1.0f);
        const float aa_fc = fmaxf(20000.0f - norm * 12000.0f, 8000.0f);
        aa_coef_ = 1.0f - expf(-2.0f * 3.14159265f * aa_fc * INV_SAMPLE_RATE);
    }

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

    const double target_samps = static_cast<double>(params.time) * SAMPLE_RATE;
    if (delay_smooth_ < 0.0) delay_smooth_ = target_samps;
    const float lfo_val = lfo_.Process();
    const float flutter = params.mod_dep * 50.0f;
    const double previous_delay = delay_smooth_;
    {
        double step = kDelaySlew * (target_samps - delay_smooth_);
        if (step >  0.5) step =  0.5;
        if (step < -0.5) step = -0.5;
        delay_smooth_ += step;
        if (fabs(target_samps - delay_smooth_) < 0.001) delay_smooth_ = target_samps;
    }
    const float delay_samps = static_cast<float>(delay_smooth_ + lfo_val * flutter);
    const float read_rate = static_cast<float>(1.0 - (delay_smooth_ - previous_delay))
                          - (lfo_val - previous_lfo_) * flutter;
    previous_lfo_ = lfo_val;
    const bool moving = flutter > 0.00001f || fabs(target_samps - delay_smooth_) > 0.001;

    // Integer taps keep static tape repeats spectrally flat before intentional
    // tape coloration; moving heads use band-limited interpolation.
    float wet_l = moving ? tape_line_l_.ReadAtResampled(delay_samps, read_rate)
                         : tape_line_l_.ReadNearest(delay_samps);

    // Read Right tap (secondary play head, offset by 150 samples / ~3.1ms).
    const float delay_r = delay_samps + spread_.Update(kStereoOffsetSamples, params.width);
    float wet_r = (moving || spread_.Fractional()) ? tape_line_r_.ReadAtResampled(delay_r, read_rate)
                         : tape_line_r_.ReadNearest(delay_r);

    // Keep each input and feedback history independent. The right head remains
    // offset for the original tape-style width, but anti-phase stereo no longer
    // cancels before it reaches the delay buffers.
    auto colorFeedback = [&](float wet, ToneFilter& filter, AntiAliasedTapeClip& saturator, float& preShelf,
                             float& postShelf, float& envelope, float& tapeLp) {
        preShelf += shelf_coef_ * (wet - preShelf);
        float colored = wet + shelf_gain_ * (wet - preShelf);
        colored = filter.Process(colored);
        const float drive = 1.0f + 3.0f * params.grit * params.grit;
        if (params.grit > 0.00001f) {
            colored += params.grit * (saturator.Process(colored, drive) - colored);
        }
        postShelf += shelf_coef_ * (colored - postShelf);
        colored -= shelf_gain_ * (colored - postShelf);
        const float magnitude = colored >= 0.0f ? colored : -colored;
        envelope += 0.05f * (magnitude - envelope);
        float lp = 0.45f - 0.35f * envelope * params.grit;
        if (lp < 0.05f) lp = 0.05f;
        tapeLp += lp * (colored - tapeLp);
        // Keep the small-signal gain at unity so Repeats sets the decay.
        return tapeLp;
    };

    const float colored_l = colorFeedback(wet_l, filter_l_, sat_l_, pre_shelf_state_l_,
                                          post_shelf_state_l_, env_state_l_, tape_lp_l_);
    const float colored_r = colorFeedback(wet_r, filter_r_, sat_r_, pre_shelf_state_r_,
                                          post_shelf_state_r_, env_state_r_, tape_lp_r_);
    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(colored_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(colored_r * params.repeats));
    auto write = [&](float dry, float feedback, float& state, DelayLineSdram& line) {
        // Preserve the clean record path below the knee and bound loop peaks.
        const float value = soft_limit_above(dry + feedback, 0.7f);
        state += aa_coef_ * (value - state);
        line.Write(value + aa_mix_ * (state - value));
    };
    write(input.left, feedback_l, aa_state_l_, tape_line_l_);
    write(input.right, feedback_r, aa_state_r_, tape_line_r_);

    // Apply DC blockers independently per channel
    wet_l = dc_l_.Process(colored_l) * filter_l_.LoudnessCorrection();
    wet_r = dc_r_.Process(colored_r) * filter_r_.LoudnessCorrection();

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
