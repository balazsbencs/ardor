#include "chorus_mode.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

static constexpr float PI_2_3 = 2.094395f; // 2π/3 = 120°

void ChorusMode::Init() {
    chorus_line_.Init(chorus_buf_, kChorusBufSize);
    for (int i = 0; i < 3; ++i) {
        lfo_[i].Init(0.5f, LfoWave::Sine);
        if (i == 0) lfo_[i].SetJitter(0.3f);   // leader jitters; the others follow
        lfo_[i].SetPhaseOffset(static_cast<float>(i) * PI_2_3);
        lfo_[i].Reset();  // apply offset to phase_ (SetPhaseOffset alone does not)
    }
    dc_.Init();
    dc_r_.Init();

    tone_l_.Init(SAMPLE_RATE, ToneGain::Loudness);
    tone_r_.Init(SAMPLE_RATE, ToneGain::Loudness);

    shifter_l_.Init(detune_buf_l_, kDetuneBufSize, SAMPLE_RATE);
    shifter_r_.Init(detune_buf_r_, kDetuneBufSize, SAMPLE_RATE);
}

void ChorusMode::Reset() {
    chorus_line_.Reset();
    for (auto& l : lfo_) l.Reset();
    dc_.Init();
    dc_r_.Init();
    bbd_.Reset();
    bbd_r_.Reset();
    shifter_l_.Reset();
    shifter_r_.Reset();
    tone_l_.Reset();
    tone_r_.Reset();
    rand_       = 12345;
    sub_mode_   = 4;
    delay_seeded_ = false;
    base_target_  = 48.0f;
    depth_target_ = 0.0f;
    base_samps_ = 48.0f;
    mod_depth_  = 0.0f;
    fb_samp_    = 0.0f;
    feedback_   = 0.0f;
    delays_[0]  = 0.0f;
    delays_[1]  = 0.0f;
    delays_[2]  = 0.0f;
}

void ChorusMode::Prepare(const ParamSet& params) {
    // Sub-mode: 0=dBucket, 1=Multi, 2=Vibrato, 3=Detune, 4=Digital
    // Dead bands stop an automated control from re-selecting at a boundary,
    // matching FilterDelay and PatternDelay. The delay line is deliberately NOT
    // cleared on a change: its contents are valid audio history whichever
    // sub-mode reads them, and the memset it used to do dropped the output.
    const float scaled = params.p2 * 4.999f;
    const int candidate = static_cast<int>(scaled);
    if (candidate > sub_mode_ && scaled > static_cast<float>(sub_mode_ + 1) + 0.06f) {
        sub_mode_ = candidate;
    } else if (candidate < sub_mode_ && scaled < static_cast<float>(sub_mode_) - 0.06f) {
        sub_mode_ = candidate;
    }

    for (auto& l : lfo_) l.SetRate(params.speed);

    if (sub_mode_ == 0) {
        // dBucket (CE-2w style): triangle LFO matches the CE-2's clock-modulation
        // waveform; gives a constant pitch-shift magnitude each half-cycle.
        lfo_[0].SetWave(LfoWave::Triangle);
        lfo_[1].SetWave(LfoWave::Triangle);
        // CE-2 center delay 3–8 ms (144–384 samples at 48 kHz).
        base_target_ = 144.0f + params.p1 * 240.0f;
        // CE-2 modulation depth: ±2 ms max (96 samples).
        depth_target_ = fminf(params.depth * 96.0f, base_target_ - 1.0f);
        // Fixed 10 % feedback, the lushness of the original circuit. Tone used
        // to set this, which left Tone doing nothing in the other four types;
        // it is now the wet tone control everywhere.
        feedback_ = 0.10f;
    } else {
        lfo_[0].SetWave(LfoWave::Sine);
        lfo_[1].SetWave(LfoWave::Sine);
        // Base delay. A chorus sits at 5..25 ms (240..1200 samples); below
        // that it combs like a flanger. Vibrato is wet only, so its delay is
        // pure latency: it keeps a short 1..10 ms range.
        base_target_ = (sub_mode_ == 2) ? 48.0f + params.p1 * 432.0f
                                        : 240.0f + params.p1 * 960.0f;
        // LFO depth: ±10 ms max, capped so delay never goes below 1 sample
        depth_target_ = fminf(params.depth * 480.0f, base_target_ - 1.0f);
        feedback_   = 0.0f;
    }

    if (sub_mode_ == 3) {
        // Detune: pitch shift L down and R up.
        // Depth controls maximum shift: 0 to 30 cents (0.3 semitones).
        const float shift_semitones = params.depth * 0.30f;
        shifter_l_.SetShift(-shift_semitones);
        shifter_r_.SetShift(shift_semitones);
    }
    if (sub_mode_ == 0) bbd_.SetClockDelaySamples(base_target_);
    tone_l_.SetKnob(params.tone);
    tone_r_.SetKnob(params.tone);
    // Multi and single-voice: LFO advanced per-sample in Process() to avoid
    // block-boundary delay jumps that cause zipper noise at high LFO rates.
}

StereoFrame ChorusMode::Process(StereoFrame input, const ParamSet& params) {
    const float kBufMax = static_cast<float>(kChorusBufSize - 2);

    // Glide the base delay and modulation depth. Prepare() only sets targets:
    // assigning them straight to the per-sample values moved the tap by up to
    // 912 samples in one step, which ticks under MIDI or expression automation.
    if (!delay_seeded_) {
        delay_seeded_ = true;
        base_samps_ = base_target_;
        mod_depth_  = depth_target_;
    }
    base_samps_ += kDelaySlew * (base_target_  - base_samps_);
    mod_depth_  += kDelaySlew * (depth_target_ - mod_depth_);

    // dBucket: mix feedback into the write source before BBD pre-coloration.
    // This is the output-to-input feedback path that gives CE-2 lushness.
    float write_in = input.mono();
    if (sub_mode_ == 0) {
        write_in = bbd_.Process(write_in + feedback_ * fb_samp_, 0.15f, rand_, base_samps_);
    }

    // Every type writes the line: Detune reads it as its pre-delay.
    chorus_line_.Write(write_in);

    float wet_l, wet_r;

    if (sub_mode_ == 1) {
        // Multi: per-sample LFO for all 3 taps (no block-boundary jumps)
        for (int i = 0; i < 3; ++i) {
            if (i != 0) lfo_[i].FollowPhaseOf(lfo_[0]);
            float d = base_samps_ + mod_depth_ * lfo_[i].Process();
            if (d < 1.0f) d = 1.0f;
            if (d > kBufMax) d = kBufMax;
            delays_[i] = d;
        }
        const float t0 = chorus_line_.ReadAtHighQuality(delays_[0]);
        const float t1 = chorus_line_.ReadAtHighQuality(delays_[1]);
        const float t2 = chorus_line_.ReadAtHighQuality(delays_[2]);
        wet_l = (t0 + t1) * 0.5f;
        wet_r = (t0 + t2) * 0.5f;
    } else if (sub_mode_ == 3) {
        // True Detune: pitch shift L down and R up. Shifting rather than
        // sweeping a delay gives a wide, lush stereo field without the
        // comb-filter notches of a modulated tap.
        // Delay sets a pre-delay ahead of the shifters, as studio micro-pitch
        // units do; it separates the detuned voices from the dry note.
        const float delayed = chorus_line_.ReadAtHighQuality(base_samps_);
        wet_l = shifter_l_.Process(delayed);
        wet_r = shifter_r_.Process(delayed);
    } else if (sub_mode_ == 0) {
        // dBucket CE-2w: two stereo taps at 120° LFO phase offset.
        // lfo_[0] drives L, lfo_[1] (initialised 120° ahead) drives R.
        float d_l = base_samps_ + mod_depth_ * lfo_[0].Process();
        lfo_[1].FollowPhaseOf(lfo_[0]);
        float d_r = base_samps_ + mod_depth_ * lfo_[1].Process();
        if (d_l < 1.0f) d_l = 1.0f;
        if (d_l > kBufMax) d_l = kBufMax;
        if (d_r < 1.0f) d_r = 1.0f;
        if (d_r > kBufMax) d_r = kBufMax;
        wet_l = bbd_.Deemphasis(chorus_line_.ReadAtHighQuality(d_l));
        wet_r = bbd_r_.Deemphasis(chorus_line_.ReadAtHighQuality(d_r));
        // Average feeds back; keeps the summed signal from building up.
        fb_samp_ = (wet_l + wet_r) * 0.5f;
    } else if (sub_mode_ == 2) {
        // Vibrato: one tap for both channels, so the pitch moves together.
        float d = base_samps_ + mod_depth_ * lfo_[0].Process();
        if (d < 1.0f) d = 1.0f;
        if (d > kBufMax) d = kBufMax;
        wet_l = chorus_line_.ReadAtHighQuality(d);
        wet_r = wet_l;
    } else {
        // Digital: two taps at 120° LFO offset for width
        float d_l = base_samps_ + mod_depth_ * lfo_[0].Process();
        lfo_[1].FollowPhaseOf(lfo_[0]);
        float d_r = base_samps_ + mod_depth_ * lfo_[1].Process();
        if (d_l < 1.0f) d_l = 1.0f;
        if (d_l > kBufMax) d_l = kBufMax;
        if (d_r < 1.0f) d_r = 1.0f;
        if (d_r > kBufMax) d_r = kBufMax;
        wet_l = chorus_line_.ReadAtHighQuality(d_l);
        wet_r = chorus_line_.ReadAtHighQuality(d_r);
    }

    wet_l = tone_l_.Process(dc_.Process(wet_l));
    wet_r = tone_r_.Process(dc_r_.Process(wet_r));

    // Vibrato is all wet: any dry signal would turn it back into a chorus.
    if (sub_mode_ == 2) return {wet_l, wet_r};
    const float mix = params.mix;
    return {input.left + mix * (wet_l - input.left), input.right + mix * (wet_r - input.right)};
}

} // namespace pedal
