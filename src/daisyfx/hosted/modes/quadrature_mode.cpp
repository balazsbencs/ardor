#include "quadrature_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"

using namespace pedal::mod_fx;

namespace pedal {

static constexpr float kTwoPi  = 6.28318530f;
static constexpr float kHalfPi = 1.57079633f;

// Wrap x into [0, 2π). Phase increments are small so the loop is at most once.
static float wrap2pi(float x) noexcept {
    if (x >= kTwoPi) x -= kTwoPi;
    if (x < 0.0f)    x += kTwoPi;
    return x;
}

// Cosine via fast_sin: cos(x) = sin(x + π/2).
static float fcos(float x) noexcept {
    return fast_sin(wrap2pi(x + kHalfPi));
}

// ---------------------------------------------------------------------------

void QuadratureMode::Init() {
    hilbert_.Init();
    lfo_.Init(1.0f, LfoWave::Sine);
    dc_.Init();
    dc_r_.Init();
    // Loudness-neutral: Tone sits after the feedback tap, outside the loop.
    tone_l_.Init(SAMPLE_RATE, ToneGain::Loudness);
    tone_r_.Init(SAMPLE_RATE, ToneGain::Loudness);
    carrier_phase_ = 0.0f;
    phase_inc_     = 0.0f;
    sub_mode_      = 0;
}

void QuadratureMode::Reset() {
    hilbert_.Reset();
    lfo_.Init(1.0f, LfoWave::Sine);
    dc_.Init();
    dc_r_.Init();
    tone_l_.Reset();
    tone_r_.Reset();
    carrier_phase_ = 0.0f;
    phase_inc_     = 0.0f;
    feedback_      = 0.0f;
    sub_mode_      = 0;
}

void QuadratureMode::Prepare(const ParamSet& params) {
    sub_mode_ = static_cast<int>(params.p2 * 3.999f);
    if (sub_mode_ < 0) sub_mode_ = 0;
    if (sub_mode_ > 3) sub_mode_ = 3;

    const float sr = static_cast<float>(SAMPLE_RATE);
    phase_inc_ = kTwoPi * params.speed / sr;
    tone_l_.SetKnob(params.tone);
    tone_r_.SetKnob(params.tone);

    if (sub_mode_ == 1) {
        lfo_.SetRate(params.speed);
    }
}

StereoFrame QuadratureMode::Process(StereoFrame input, const ParamSet& params) {
    // AM (sub_mode_=0) uses raw input for ring-mod; Hilbert is only needed for
    // FM/FreqShift modes. Skip the 8-allpass transform in the AM path.
    const float mono = input.mono();
    float re = mono, im = 0.0f;
    if (sub_mode_ != 0) {
        // Shift feedback (Depth, up to 90 %): the shifted output re-enters
        // the shifter, so each pass moves a further step and the partials
        // spiral. The soft clip keeps the loop bounded near its top.
        const float fed = sub_mode_ >= 2
            ? mono + soft_clip_tanh(feedback_ * params.depth * 0.9f)
            : mono;
        const auto frame = hilbert_.Process(fed);
        re = frame.re; im = frame.im;
    }

    // Advance carrier phase — FM mode varies instantaneous rate with LFO.
    if (sub_mode_ == 1) {
        const float lfo_val  = lfo_.Process();  // -1..+1
        const float sr       = static_cast<float>(SAMPLE_RATE);
        const float inst_freq = params.depth * 80.0f * lfo_val;
        const float inst_inc = kTwoPi * inst_freq / sr;
        carrier_phase_ = wrap2pi(carrier_phase_ + inst_inc);
    } else {
        carrier_phase_ = wrap2pi(carrier_phase_ + phase_inc_);
    }

    const float cos_c = fcos(carrier_phase_);
    const float sin_c = fast_sin(carrier_phase_);

    switch (sub_mode_) {
        case 0: {
            // AM — Depth blends from the dry note (0) to ring modulation (1):
            // gain = 1 - d + d * carrier. Right blends from the same carrier
            // to its quadrature partner, for stereo rotation (P1). Depth used
            // to do nothing here: the mode was ring modulation or nothing.
            // No DC blocker: an AC input times a carrier makes no DC, and one
            // would phase-shift the dry note at zero depth.
            const float d = params.depth;
            const float p1 = params.p1;
            const float carrier_r = cos_c * (1.0f - p1) - sin_c * p1;
            const float left  = mono * (1.0f - d + d * cos_c);
            const float right = mono * (1.0f - d + d * carrier_r);
            return {tone_l_.Process(left), tone_r_.Process(right)};
        }

        case 1: {
            // Warble — an LFO-swept single-sideband frequency shift.
            //
            // This is NOT pitch vibrato, and cannot be: vibrato is a
            // multiplicative ratio on every partial, while single-sideband
            // rotation adds a constant number of hertz to all of them. On a
            // single note the difference is subtle; on a chord the additive
            // shift breaks the harmonic relationships. Named for what it does.
            const float wet = dc_.Process(re * cos_c - im * sin_c);
            return {tone_l_.Process(wet), tone_r_.Process(wet)};
        }

        case 2: {
            // FreqShift+ — single-sideband upward shift.
            // P1 blends shifted signal with unshifted real part.
            const float shifted = re * cos_c - im * sin_c;
            feedback_ = shifted;
            const float blend   = params.p1;
            const float out     = dc_.Process(shifted * (1.0f - blend) + re * blend);
            const float shaped = tone_l_.Process(out);
            return {shaped, tone_r_.Process(out)};
        }

        case 3: {
            // FreqShift- — single-sideband downward shift.
            const float shifted = re * cos_c + im * sin_c;
            feedback_ = shifted;
            const float blend   = params.p1;
            const float out     = dc_.Process(shifted * (1.0f - blend) + re * blend);
            const float shaped = tone_l_.Process(out);
            return {shaped, tone_r_.Process(out)};
        }

        default:
            return {mono, mono};
    }
}

} // namespace pedal
