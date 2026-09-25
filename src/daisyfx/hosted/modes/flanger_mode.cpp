#include "flanger_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

void FlangerMode::Init() {
    flanger_line_l_.Init(flanger_buf_l_, kFlangerBufSize);
    flanger_line_r_.Init(flanger_buf_r_, kFlangerBufSize);

    lfo_.Init(0.3f, LfoWave::Sine);
    lfo_r_.Init(0.3f, LfoWave::Sine);
    lfo_r_.SetPhaseOffset(1.5707963f);  // π/2 radians = 90° stereo spread
    lfo_r_.Reset();  // apply offset to phase_

    dc_l_.Init();
    dc_r_.Init();

    // Initialize custom low-pass filters with a 0.5 coefficient for dark repeats
    fb_lpf_l_.Init(0.5f);
    fb_lpf_r_.Init(0.5f);

    dry_delay_l_.Init();
    dry_delay_r_.Init();
    rand_state_ = 12345;
    drift_l_ = 0.0f;
    drift_r_ = 0.0f;
    centre_seeded_ = false;
}

void FlangerMode::Reset() {
    flanger_line_l_.Reset();
    flanger_line_r_.Reset();
    lfo_.Reset();
    lfo_r_.SetPhaseOffset(1.5707963f);
    lfo_r_.Reset();

    dc_l_.Init();
    dc_r_.Init();

    fb_lpf_l_.Init(0.5f);
    fb_lpf_r_.Init(0.5f);

    dry_delay_l_.Init();
    dry_delay_r_.Init();
    rand_state_ = 12345;
    drift_l_ = 0.0f;
    drift_r_ = 0.0f;
    centre_seeded_ = false;
}

void FlangerMode::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.speed);
    lfo_r_.SetRate(params.speed);

    // Sub-mode from p2: 0=Silver, 1=Grey, 2=Black+, 3=Black-, 4=Zero+, 5=Zero-
    const int sub = static_cast<int>(params.p2 * 5.999f);

    // Determine feedback sign and base depth from sub-mode
    switch (sub) {
        case 0: fb_sign_ =  1.0f; break;  // Silver: positive, moderate
        case 1: fb_sign_ = -1.0f; break;  // Grey: negative, hollow
        case 2: fb_sign_ =  1.0f; break;  // Black+: positive, high regen
        case 3: fb_sign_ = -1.0f; break;  // Black-: negative, high regen
        case 4: fb_sign_ =  1.0f; break;  // Zero+: through-zero emulation (positive)
        case 5: fb_sign_ = -1.0f; break;  // Zero-: through-zero emulation (negative)
        default: fb_sign_ = 1.0f; break;
    }

    // Select LFO waveform based on sub-mode
    LfoWave wave;
    switch (sub) {
        case 0: wave = LfoWave::Sine; break;
        case 1: wave = LfoWave::Triangle; break;
        case 2: wave = LfoWave::Triangle; break;
        case 3: wave = LfoWave::Sine; break;
        case 4: wave = LfoWave::Exponential; break;
        case 5: wave = LfoWave::Exponential; break;
        default: wave = LfoWave::Sine; break;
    }
    lfo_.SetWave(wave);
    lfo_r_.SetWave(wave);

    // Through-Zero modes: add small LFO jitter for organic tape speed instability
    if (sub >= 4) {
        lfo_.SetJitter(0.15f);   // leader only; lfo_r_ follows it in Process()
    } else {
        lfo_.SetJitter(0.0f);
    }

    // Map TONE param (0..1) to feedback LPF cutoff coefficient (0.05..1.0)
    // 0.5 maps to 0.525, very close to original 0.5 BBD warmth coefficient.
    const float lp_coeff = 0.05f + 0.95f * params.tone;
    fb_lpf_l_.coeff = lp_coeff;
    fb_lpf_r_.coeff = lp_coeff;

    // Black and Zero sub-modes allow higher depth
    max_depth_ = (sub >= 2) ? 460.0f : 240.0f;
    depth_ = params.depth;

    // Manual (p3) places the sweep centre; 0.5 is the fixed centre this mode
    // had before the control. In the through-zero types the dry path stays
    // put, so Manual sets how far the wet tap passes through zero.
    centre_target_ = max_depth_ * params.p3;

    // Stereo (p4): 0..180 degrees between the L and R sweeps. 0.5 is the
    // 90 degrees this mode always used. At 0 the channels share one sweep and
    // one drift, so a mono source stays mono.
    lfo_r_.SetPhaseOffset(params.p4 * 3.14159265f);
    drift_spread_ = params.p4 * 2.0f > 1.0f ? 1.0f : params.p4 * 2.0f;
}

StereoFrame FlangerMode::Process(StereoFrame input, const ParamSet& params) {
    // 0. Store dry input in our pure delay buffers
    dry_delay_l_.Write(input.left);
    dry_delay_r_.Write(input.right);

    // 1. Advance both LFOs per-sample; right channel leads by π/2 for stereo spread
    //    Follow before the leader advances, so both read the same instant and
    //    a 0 degree Stereo setting gives identical sweeps.
    lfo_r_.FollowPhaseOf(lfo_);   // hold the stereo spread exactly
    const float lfo_r = lfo_r_.Process();
    const float lfo_l = lfo_.Process();

    // 2. Update slow-moving organic drift (wow and flutter emulation).
    // A one-pole on uniform noise settles at sigma*sqrt(a/2); at a = 0.0002
    // that is 0.006 samples, or 0.12 us — two orders of magnitude below
    // audibility, so the original constants cost cycles and delivered nothing.
    // kDriftGain scales it to a few samples of genuine wow.
    drift_l_ += kDriftCoeff * (lcg_to_float(rand_state_) - drift_l_);
    rand_state_ = lcg_next(rand_state_);
    drift_r_ += kDriftCoeff * (lcg_to_float(rand_state_) - drift_r_);
    rand_state_ = lcg_next(rand_state_);

    const int sub = static_cast<int>(params.p2 * 5.999f);
    const float drift_amt = kDriftGain * ((sub >= 4) ? 1.5f : 0.8f);
    const float drift_r = drift_spread_ >= 1.0f
        ? drift_r_ : drift_l_ + drift_spread_ * (drift_r_ - drift_l_);

    // 3. Calculate delays and clamp to buffer bounds. The sweep sits on
    //    kMinDelay so its whole range is audible; see the header. The swing
    //    narrows as Manual nears either end, so the sweep stays in range.
    if (!centre_seeded_) {
        centre_seeded_ = true;
        centre_ = centre_target_;
    }
    centre_ += kCentreSlew * (centre_target_ - centre_);
    const float headroom = max_depth_ - centre_;
    const float swing = depth_ * (centre_ < headroom ? centre_ : headroom);
    float delay_l = kMinDelay + centre_ + lfo_l * swing + drift_l_ * drift_amt;
    float delay_r = kMinDelay + centre_ + lfo_r * swing + drift_r * drift_amt;
    if (delay_l < kMinDelay) delay_l = kMinDelay;
    if (delay_r < kMinDelay) delay_r = kMinDelay;
    if (delay_l >= static_cast<float>(kFlangerBufSize - 1)) delay_l = static_cast<float>(kFlangerBufSize - 1);
    if (delay_r >= static_cast<float>(kFlangerBufSize - 1)) delay_r = static_cast<float>(kFlangerBufSize - 1);

    // 4. Read modulated taps with cubic interpolation
    float wet_l_tap = flanger_line_l_.ReadAtHighQuality(delay_l);
    float wet_r_tap = flanger_line_r_.ReadAtHighQuality(delay_r);

    // 5. Calculate Feedback with Saturation and Low-Pass Damping using the pure modulated taps
    float regen = params.p1 * 0.95f;

    // Soft clip the feedback to sound "analog" and prevent digital harshness
    float fb_l = soft_clip_tanh(wet_l_tap * regen * fb_sign_);
    float fb_r = soft_clip_tanh(wet_r_tap * regen * fb_sign_);

    // Apply our 1-pole Low Pass Filter to the feedback loop
    fb_l = fb_lpf_l_.Process(fb_l);
    fb_r = fb_lpf_r_.Process(fb_r);

    // 6. Write to delay line
    flanger_line_l_.Write(input.left + fb_l);
    flanger_line_r_.Write(input.right + fb_r);

    // 7. DC block the modulated taps, then take some of the resonance's gain
    //    back out.
    //
    // A feedback comb is loud at its resonance. A sustained tone sitting on one
    // measured +9.4 dB through this block at the regen the shipped presets use,
    // and +9.9 dB at the top of the control, which is enough to push whatever
    // follows into clipping. Turning up a flanger's regen should not spend the
    // chain's headroom; on a real pedal you would pull the level knob back, but
    // in a fixed chain the block has to behave.
    //
    // The constant is fitted to the measured curve, not derived. The textbook
    // peak gain of a comb, 1/(1 - regen), reaches +18 dB here — far more than
    // the circuit actually does, because the low pass in the feedback path and
    // the saturation there both hold it down.
    //
    // It deliberately does not flatten the peak. A single gain cannot fix both
    // ends of this: the resonance is narrow band, so taking all of it out costs
    // broadband level, and the flanger would get quieter as regen went up,
    // which is the wrong way round. Measured on a 0.4 % frequency grid across
    // the control, the value below leaves the worst-case peak at +3.6 dB
    // instead of +9.4. The earlier 1.2 was fitted on a 15 % grid that stepped
    // over the resonance; its true peak was +4.5 dB. 1.5 costs 1.1 dB more
    // wet level at the top of the control than 1.2 did.
    static constexpr float kRegenMakeup = 1.5f;
    const float resonance_makeup = 1.0f / (1.0f + kRegenMakeup * regen);

    float wet_l = dc_l_.Process(wet_l_tap) * resonance_makeup;
    float wet_r = dc_r_.Process(wet_r_tap) * resonance_makeup;

    // 8. Blend dry and wet. The host hands over its smoothed Mix because this
    //    mode owns the blend (see OwnsDryMix()).
    //
    // Through-zero types take their dry path from a fixed delay equal to the
    // centre of the wet sweep, so the wet tap passes through the dry one and
    // the notches cancel completely at the zero point. The dry path must be
    // only the delayed copy: the earlier version kept the host's undelayed dry
    // and tried to cancel it with a ratio that was exact only at 50 % Mix,
    // leaving up to 27 % of undelayed dry to comb against the delayed copy.
    const float mix = params.mix;
    float dry_l = input.left;
    float dry_r = input.right;
    if (sub >= 4) {
        // The dry path sits at the middle of the full range, not at Manual:
        // moving it would pitch-bend the dry signal.
        const size_t dry_delay = static_cast<size_t>(kMinDelay + max_depth_ * 0.5f);
        dry_l = dry_delay_l_.Read(dry_delay);
        dry_r = dry_delay_r_.Read(dry_delay);
        // Zero- inverts the wet path so the two cancel at the zero point.
        wet_l *= fb_sign_;
        wet_r *= fb_sign_;
    }
    // Equal-power blend: across the spectrum a comb's wet copy is on average
    // uncorrelated with the dry, so a linear 50/50 blend lost 3.4 dB. Equal
    // gains at 50 % still give full-depth notches and a full through-zero
    // cancellation.
    const float angle = mix * 1.57079633f;
    const float dry_gain = fast_cos(angle);
    const float wet_gain = fast_sin(angle);
    wet_l = dry_l * dry_gain + wet_l * wet_gain;
    wet_r = dry_r * dry_gain + wet_r * wet_gain;

    return {wet_l, wet_r};
}

} // namespace pedal
