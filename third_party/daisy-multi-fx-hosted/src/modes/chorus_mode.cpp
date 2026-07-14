#include "chorus_mode.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

// Chorus needs a longer delay than MAX_MOD_DELAY_SAMPLES (25ms).
// Use 50ms = 2400 samples; SDRAM buffer cost is minimal.
static constexpr size_t kChorusBufSize = 2400;
static float DSY_SDRAM_BSS s_chorus_buf[kChorusBufSize];
static DelayLineSdram s_chorus_line;

// Detune mode pitch shifters
static float DSY_SDRAM_BSS detune_buf_l[4096];
static float DSY_SDRAM_BSS detune_buf_r[4096];
static PitchShifter s_shifter_l;
static PitchShifter s_shifter_r;

static constexpr float PI_2_3 = 2.094395f; // 2π/3 = 120°

void ChorusMode::Init() {
    s_chorus_line.Init(s_chorus_buf, kChorusBufSize);
    for (int i = 0; i < 3; ++i) {
        lfo_[i].Init(0.5f, LfoWave::Sine);
        lfo_[i].SetJitter(0.3f);
        lfo_[i].SetPhaseOffset(static_cast<float>(i) * PI_2_3);
        lfo_[i].Reset();  // apply offset to phase_ (SetPhaseOffset alone does not)
    }
    dc_.Init();
    dc_r_.Init();

    s_shifter_l.Init(detune_buf_l, 4096, SAMPLE_RATE);
    s_shifter_r.Init(detune_buf_r, 4096, SAMPLE_RATE);
    shifter_l_ = &s_shifter_l;
    shifter_r_ = &s_shifter_r;
}

void ChorusMode::Reset() {
    s_chorus_line.Reset();
    for (auto& l : lfo_) l.Reset();
    dc_.Init();
    dc_r_.Init();
    bbd_.Reset();
    bbd_r_.Reset();
    shifter_l_->Reset();
    shifter_r_->Reset();
    rand_       = 12345;
    sub_mode_   = 4;
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
    int new_sub_mode = static_cast<int>(params.p2 * 4.999f);
    if (new_sub_mode != sub_mode_) {
        sub_mode_ = new_sub_mode;
        s_chorus_line.Reset();
    }

    for (auto& l : lfo_) l.SetRate(params.speed);

    if (sub_mode_ == 0) {
        // dBucket (CE-2w style): triangle LFO matches the CE-2's clock-modulation
        // waveform; gives a constant pitch-shift magnitude each half-cycle.
        lfo_[0].SetWave(LfoWave::Triangle);
        lfo_[1].SetWave(LfoWave::Triangle);
        // CE-2 center delay 3–8 ms (144–384 samples at 48 kHz).
        base_samps_ = 144.0f + params.p1 * 240.0f;
        // CE-2 modulation depth: ±2 ms max (96 samples).
        mod_depth_  = fminf(params.depth * 96.0f, base_samps_ - 1.0f);
        // Tone controls feedback depth (0–20%). CE-2 has ~10–15% fixed; center
        // knob (0.5) lands at 10%, matching the original circuit.
        feedback_ = params.tone * 0.20f;
    } else {
        lfo_[0].SetWave(LfoWave::Sine);
        lfo_[1].SetWave(LfoWave::Sine);
        // Base delay: p1 maps 1 ms..20 ms → 48..960 samples
        base_samps_ = 48.0f + params.p1 * 912.0f;
        // LFO depth: ±10 ms max, capped so delay never goes below 1 sample
        mod_depth_  = fminf(params.depth * 480.0f, base_samps_ - 1.0f);
        feedback_   = 0.0f;
    }

    if (sub_mode_ == 3) {
        // Detune: pitch shift L down and R up.
        // Depth controls maximum shift: 0 to 30 cents (0.3 semitones).
        const float shift_semitones = params.depth * 0.30f;
        shifter_l_->SetShift(-shift_semitones);
        shifter_r_->SetShift(shift_semitones);
    }
    // Multi and single-voice: LFO advanced per-sample in Process() to avoid
    // block-boundary delay jumps that cause zipper noise at high LFO rates.
}

StereoFrame ChorusMode::Process(StereoFrame input, const ParamSet& params) {
    const float kBufMax = static_cast<float>(kChorusBufSize - 2);

    // dBucket: mix feedback into the write source before BBD pre-coloration.
    // This is the output-to-input feedback path that gives CE-2 lushness.
    float write_in = input.mono();
    if (sub_mode_ == 0) {
        write_in = bbd_.Process(write_in + feedback_ * fb_samp_, 0.15f, rand_, base_samps_);
    }

    if (sub_mode_ != 3) {
        s_chorus_line.Write(write_in);
    }

    float wet_l, wet_r;

    if (sub_mode_ == 1) {
        // Multi: per-sample LFO for all 3 taps (no block-boundary jumps)
        for (int i = 0; i < 3; ++i) {
            float d = base_samps_ + mod_depth_ * lfo_[i].Process();
            if (d < 1.0f) d = 1.0f;
            if (d > kBufMax) d = kBufMax;
            delays_[i] = d;
        }
        const float t0 = s_chorus_line.ReadAt(delays_[0]);
        const float t1 = s_chorus_line.ReadAt(delays_[1]);
        const float t2 = s_chorus_line.ReadAt(delays_[2]);
        wet_l = (t0 + t1) * 0.5f;
        wet_r = (t0 + t2) * 0.5f;
    } else if (sub_mode_ == 3) {
        // True Detune: pitch shift L down and R up.
        // It operates directly on the input signal, creating a wide, lush
        // pitch-detuned stereo field without any comb-filtering notches!
        const float mono_in = input.mono();
        wet_l = shifter_l_->Process(mono_in);
        wet_r = shifter_r_->Process(mono_in);
    } else if (sub_mode_ == 0) {
        // dBucket CE-2w: two stereo taps at 120° LFO phase offset.
        // lfo_[0] drives L, lfo_[1] (initialised 120° ahead) drives R.
        float d_l = base_samps_ + mod_depth_ * lfo_[0].Process();
        float d_r = base_samps_ + mod_depth_ * lfo_[1].Process();
        if (d_l < 1.0f) d_l = 1.0f;
        if (d_l > kBufMax) d_l = kBufMax;
        if (d_r < 1.0f) d_r = 1.0f;
        if (d_r > kBufMax) d_r = kBufMax;
        wet_l = bbd_.Deemphasis(s_chorus_line.ReadAt(d_l));
        wet_r = bbd_r_.Deemphasis(s_chorus_line.ReadAt(d_r));
        // Average feeds back; keeps the summed signal from building up.
        fb_samp_ = (wet_l + wet_r) * 0.5f;
    } else {
        // Single-voice (Vibrato=2, Digital=4): two taps at 120° LFO offset for width
        float d_l = base_samps_ + mod_depth_ * lfo_[0].Process();
        float d_r = base_samps_ + mod_depth_ * lfo_[1].Process();
        if (d_l < 1.0f) d_l = 1.0f;
        if (d_l > kBufMax) d_l = kBufMax;
        if (d_r < 1.0f) d_r = 1.0f;
        if (d_r > kBufMax) d_r = kBufMax;
        wet_l = s_chorus_line.ReadAt(d_l);
        wet_r = s_chorus_line.ReadAt(d_r);
    }

    wet_l = dc_.Process(wet_l);
    wet_r = dc_r_.Process(wet_r);

    return {wet_l, wet_r};
}

} // namespace pedal
