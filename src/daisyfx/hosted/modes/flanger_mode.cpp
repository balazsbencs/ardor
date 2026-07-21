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
        lfo_.SetJitter(0.15f);
        lfo_r_.SetJitter(0.15f);
    } else {
        lfo_.SetJitter(0.0f);
        lfo_r_.SetJitter(0.0f);
    }

    // Map TONE param (0..1) to feedback LPF cutoff coefficient (0.05..1.0)
    // 0.5 maps to 0.525, very close to original 0.5 BBD warmth coefficient.
    const float lp_coeff = 0.05f + 0.95f * params.tone;
    fb_lpf_l_.coeff = lp_coeff;
    fb_lpf_r_.coeff = lp_coeff;

    // Black and Zero sub-modes allow higher depth
    max_depth_ = (sub >= 2) ? 460.0f : 240.0f;
    depth_ = params.depth;
}

StereoFrame FlangerMode::Process(StereoFrame input, const ParamSet& params) {
    // 0. Store dry input in our pure delay buffers
    dry_delay_l_.Write(input.left);
    dry_delay_r_.Write(input.right);

    // 1. Advance both LFOs per-sample; right channel leads by π/2 for stereo spread
    const float lfo_l = lfo_.Process();
    const float lfo_r = lfo_r_.Process();

    // 2. Update slow-moving organic drift (wow and flutter emulation)
    // Low filter cutoff (0.0002) makes the drift slow, smooth, and organic.
    drift_l_ += 0.0002f * (lcg_to_float(rand_state_) - drift_l_);
    rand_state_ = lcg_next(rand_state_);
    drift_r_ += 0.0002f * (lcg_to_float(rand_state_) - drift_r_);
    rand_state_ = lcg_next(rand_state_);

    const int sub = static_cast<int>(params.p2 * 5.999f);
    const float drift_amt = (sub >= 4) ? 1.5f : 0.8f; // slightly more drift in TZF modes

    // 3. Calculate delays and clamp to buffer bounds
    const float center = max_depth_ * 0.5f;
    float delay_l = center + lfo_l * depth_ * center + 1.0f + drift_l_ * drift_amt;
    float delay_r = center + lfo_r * depth_ * center + 1.0f + drift_r_ * drift_amt;
    if (delay_l < 1.0f) delay_l = 1.0f;
    if (delay_r < 1.0f) delay_r = 1.0f;
    if (delay_l >= static_cast<float>(kFlangerBufSize - 1)) delay_l = static_cast<float>(kFlangerBufSize - 1);
    if (delay_r >= static_cast<float>(kFlangerBufSize - 1)) delay_r = static_cast<float>(kFlangerBufSize - 1);

    // 4. Read modulated taps with cubic interpolation
    float wet_l_tap = flanger_line_l_.ReadAt(delay_l);
    float wet_r_tap = flanger_line_r_.ReadAt(delay_r);

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

    // 7. DC block the modulated taps
    float wet_l = dc_l_.Process(wet_l_tap);
    float wet_r = dc_r_.Process(wet_r_tap);

    // 8. If Through-Zero Flanger, compensate for external engine dry mix
    if (sub >= 4) {
        static constexpr float kDryDelay = 230.0f;
        float dry_delayed_l = dry_delay_l_.Read(static_cast<size_t>(kDryDelay));
        float dry_delayed_r = dry_delay_r_.Read(static_cast<size_t>(kDryDelay));

        // ZERO- (sub 5) inverts the phase of the wet signal to cause cancellation
        float wet_mod_l = wet_l * fb_sign_;
        float wet_mod_r = wet_r * fb_sign_;

        // Calculate dry/wet mix ratio: mod_dry / mod_wet
        float ratio = 0.0f;
        if (params.mix > 0.001f) {
            float angle = params.mix * 1.57079632679f;
            ratio = fast_cos(angle) / fast_sin(angle);
            if (ratio > 10.0f) ratio = 10.0f; // clamp to prevent division blowup
        }

        // Apply cancellation formula to override external dry path with delayed dry path
        wet_l = wet_mod_l + (dry_delayed_l - input.left) * ratio;
        wet_r = wet_mod_r + (dry_delayed_r - input.right) * ratio;
    }

    return {wet_l, wet_r};
}

} // namespace pedal
