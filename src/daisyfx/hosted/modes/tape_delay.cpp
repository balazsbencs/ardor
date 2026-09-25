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
    sat_.Init(WaveCurve::Tape);
    dc_l_.Init();
    dc_r_.Init();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
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
    if (flutter <= 0.00001f || params.mod_spd <= 0.00001f) {
        aa_coef_ = 1.0f;
    } else {
        const float mod_rate_hz = params.mod_spd * flutter;
        const float norm = mod_rate_hz / (10.0f * 50.0f);
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
    const float delay_samps = delay_smooth_ + lfo_val * flutter;
    const bool moving = flutter > 0.00001f || fabsf(target_samps - delay_smooth_) > 0.01f;

    // Integer taps keep static tape repeats spectrally flat before intentional
    // tape coloration; moving heads use band-limited interpolation.
    float wet_l = moving ? tape_line_l_.ReadAtHighQuality(delay_samps)
                         : tape_line_l_.ReadNearest(delay_samps);

    // Read Right tap (secondary play head, offset by 150 samples / ~3.1ms).
    const float delay_r = delay_samps + spread_.Update(kStereoOffsetSamples, params.width);
    float wet_r = (moving || spread_.Fractional()) ? tape_line_r_.ReadAtHighQuality(delay_r)
                                                   : tape_line_r_.ReadNearest(delay_r);

    // Playback: Tone shapes what the head reads, so it colours the first
    // repeat and, through the loop, every later one. The loop keeps the
    // loop-safe tilt; the output alone is corrected to keep its loudness.
    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);
    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(wet_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(wet_r * params.repeats));

    // Record path: everything written to tape passes the tape EQ, the
    // saturation and the head loss, so the first repeat is coloured as well as
    // the later ones. These used to sit on the feedback path only, which left
    // Tone and Grit with no effect on the first repeat.
    //
    // The saturator is normalised to unity small-signal gain and blended in by
    // Grit. Its raw gain grew to 16x with Grit, which made the repeats sustain
    // for ever at modest Repeats settings.
    const float drive = 1.0f + params.grit * params.grit * 15.0f;
    auto record = [&](float x, float& preShelf, float& postShelf, float& envelope, float& tapeLp,
                      float& aaState, DelayLineSdram& line) {
        preShelf += shelf_coef_ * (x - preShelf);
        float colored = x + shelf_gain_ * (x - preShelf);
        colored += params.grit * (sat_.Process(colored) / drive - colored);
        postShelf += shelf_coef_ * (colored - postShelf);
        colored -= shelf_gain_ * (colored - postShelf);
        const float magnitude = colored >= 0.0f ? colored : -colored;
        envelope += 0.05f * (magnitude - envelope);
        float lp = 0.45f - 0.35f * envelope * params.grit;
        if (lp < 0.05f) lp = 0.05f;
        tapeLp += lp * (colored - tapeLp);
        // Safety limit on the write, clean below -3 dBFS: the smooth knee sat
        // at 2 tanh(x/2) before, which bent every signal (-44 dBc of third
        // harmonic on a 0.6 tone at Grit 0).
        aaState += aa_coef_ * (soft_limit_above(tapeLp, 0.7f) - aaState);
        line.Write(aaState);
    };
    record(input.left + feedback_l, pre_shelf_state_l_, post_shelf_state_l_, env_state_l_, tape_lp_l_,
           aa_state_l_, tape_line_l_);
    record(input.right + feedback_r, pre_shelf_state_r_, post_shelf_state_r_, env_state_r_, tape_lp_r_,
           aa_state_r_, tape_line_r_);

    // Apply DC blockers independently per channel
    wet_l = dc_l_.Process(wet_l) * filter_l_.LoudnessCorrection();
    wet_r = dc_r_.Process(wet_r) * filter_r_.LoudnessCorrection();

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
