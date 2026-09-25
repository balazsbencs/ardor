#include "poly_octave_mode.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace pedal::mod_fx;

namespace pedal {

// ── ShelfBiquad factory methods ──────────────────────────────────────────────
// Coefficients match cycfi::q::config_highshelf / config_lowshelf exactly:
// beta = sqrt(A + A), not the bandwidth-parameterised alpha.

ShelfBiquad ShelfBiquad::make_highshelf(double db_gain, double freq_hz, double sps) noexcept
{
    const double A     = std::pow(10.0, db_gain / 40.0);
    const double beta  = std::sqrt(A + A);
    const double omega = 2.0 * std::numbers::pi_v<double> * freq_hz / sps;
    const double sinw  = std::sin(omega);
    const double cosw  = std::cos(omega);

    const double b0 =  A * ((A + 1) + (A - 1) * cosw + beta * sinw);
    const double b1 = -2.0 * A * ((A - 1) + (A + 1) * cosw);
    const double b2 =  A * ((A + 1) + (A - 1) * cosw - beta * sinw);
    const double a0 =      (A + 1) - (A - 1) * cosw + beta * sinw;
    const double a1 =  2.0 * ((A - 1) - (A + 1) * cosw);
    const double a2 =      (A + 1) - (A - 1) * cosw - beta * sinw;

    ShelfBiquad bq;
    bq.a0 = static_cast<float>(b0 / a0);
    bq.a1 = static_cast<float>(b1 / a0);
    bq.a2 = static_cast<float>(b2 / a0);
    bq.a3 = static_cast<float>(a1 / a0);
    bq.a4 = static_cast<float>(a2 / a0);
    return bq;
}

ShelfBiquad ShelfBiquad::make_lowshelf(double db_gain, double freq_hz, double sps) noexcept
{
    const double A     = std::pow(10.0, db_gain / 40.0);
    const double beta  = std::sqrt(A + A);
    const double omega = 2.0 * std::numbers::pi_v<double> * freq_hz / sps;
    const double sinw  = std::sin(omega);
    const double cosw  = std::cos(omega);

    const double b0 =  A * ((A + 1) - (A - 1) * cosw + beta * sinw);
    const double b1 =  2.0 * A * ((A - 1) - (A + 1) * cosw);
    const double b2 =  A * ((A + 1) - (A - 1) * cosw - beta * sinw);
    const double a0 =      (A + 1) + (A - 1) * cosw + beta * sinw;
    const double a1 = -2.0 * ((A - 1) + (A + 1) * cosw);
    const double a2 =      (A + 1) + (A - 1) * cosw - beta * sinw;

    ShelfBiquad bq;
    bq.a0 = static_cast<float>(b0 / a0);
    bq.a1 = static_cast<float>(b1 / a0);
    bq.a2 = static_cast<float>(b2 / a0);
    bq.a3 = static_cast<float>(a1 / a0);
    bq.a4 = static_cast<float>(a2 / a0);
    return bq;
}

// ── PolyOctaveMode ────────────────────────────────────────────────────────────

namespace {

// Make-up per voice so that each, alone at full level, sits about 1.5 dB under
// the dry note. Measured on a DI guitar recording and a synthetic phrase, the
// generator's raw voices came out at -13.7 (up), -5.7 (down) and -3.2 dB
// (down 2): the octave up was barely audible at full.
constexpr float kUp1Gain   = 4.07f;   // +12.2 dB
constexpr float kDown1Gain = 1.62f;   // +4.2 dB
constexpr float kDown2Gain = 1.22f;   // +1.7 dB

// Knob moves glide over ~20 ms so a swept level does not step.
constexpr float kLevelSmoothing = 0.001f;
// Attack envelopes. A symmetric 10 ms smoother gives the note's envelope; the
// slow copy rises at the Attack rate but falls with it at once. In sustain the
// two agree and the voices sit at full level; at each new note the slow copy
// lags and the voices swell in. Two peak detectors with different attacks
// would not agree in sustain: the slow one only rises on each cycle's peaks
// and settles lower, turning the voices down for good.
const float kEnvSmoothing = 1.0f - std::exp(-1.0f / (0.010f * SAMPLE_RATE));
constexpr float kMaxAttackSeconds = 0.25f;

} // namespace

void PolyOctaveMode::Init()
{
    octave_gen_.init(SAMPLE_RATE / static_cast<float>(resample_factor));
    eq_high_ = ShelfBiquad::make_highshelf(-11.0, 140.0, SAMPLE_RATE);
    eq_low_  = ShelfBiquad::make_lowshelf(   5.0, 160.0, SAMPLE_RATE);
    // Loudness-neutral: the voices feed no loop.
    tone_.Init(SAMPLE_RATE, ToneGain::Loudness);
    Reset();
}

void PolyOctaveMode::Reset()
{
    for (auto& s : in_buf_)  s = 0.0f;
    for (auto& s : out_buf_) s = 0.0f;
    decimator_.Reset();
    interpolator_.Reset();
    buf_idx_ = 0;
    out_idx_ = 0;
    eq_high_.reset();
    eq_low_.reset();
    tone_.Reset();
    up1_target_   = 0.0f;
    down1_target_ = 0.0f;
    down2_target_ = 0.0f;
    up1_level_    = 0.0f;
    down1_level_  = 0.0f;
    down2_level_  = 0.0f;
    dry_target_   = 1.0f;
    dry_level_    = 1.0f;
    env_fast_     = 0.0f;
    env_slow_     = 0.0f;
    attack_coefficient_ = 1.0f;
}

void PolyOctaveMode::Prepare(const ParamSet& params)
{
    // p1 → octave up 1, p2 → octave down 1, depth → octave down 2, p3 → dry.
    up1_target_   = params.p1 * kUp1Gain;
    down1_target_ = params.p2 * kDown1Gain;
    down2_target_ = params.depth * kDown2Gain;
    dry_target_   = params.p3;

    // Tone shapes the voices only; the dry note passes untouched.
    tone_.SetKnob(params.tone);

    // Attack (Speed): 0 is immediate, full is a 250 ms swell. Speed used to be
    // "Tracking", which only smoothed knob moves and measured 80 dB below the
    // signal between its two ends: the control did nothing audible.
    const float amount = std::clamp((params.speed - 0.05f) / 9.95f, 0.0f, 1.0f);
    const float seconds = amount * kMaxAttackSeconds;
    attack_coefficient_ = seconds > 0.0005f
        ? 1.0f - std::exp(-1.0f / (seconds * SAMPLE_RATE))
        : 1.0f;
}

StereoFrame PolyOctaveMode::Process(StereoFrame input, const ParamSet& /*params*/)
{
    const float mono = input.mono();
    in_buf_[buf_idx_++] = mono;

    up1_level_   += kLevelSmoothing * (up1_target_   - up1_level_);
    down1_level_ += kLevelSmoothing * (down1_target_ - down1_level_);
    down2_level_ += kLevelSmoothing * (down2_target_ - down2_level_);
    dry_level_   += kLevelSmoothing * (dry_target_   - dry_level_);

    // Attack envelope pair on the input.
    env_fast_ += kEnvSmoothing * (std::fabs(mono) - env_fast_);
    env_slow_ += attack_coefficient_ * (env_fast_ - env_slow_);
    if (env_slow_ > env_fast_) env_slow_ = env_fast_;
    const float swell = env_fast_ > 1.0e-6f ? env_slow_ / env_fast_ : 1.0f;

    if (buf_idx_ == static_cast<int>(resample_factor))
    {
        const float decimated = decimator_(
            std::span<const float, resample_factor>{in_buf_, resample_factor});

        octave_gen_.update(decimated);
        const float wet = up1_level_   * octave_gen_.up1()
                        + down1_level_ * octave_gen_.down1()
                        + down2_level_ * octave_gen_.down2();

        const auto interp = interpolator_(wet);
        for (int j = 0; j < static_cast<int>(resample_factor); ++j)
            out_buf_[j] = eq_low_(eq_high_(interp[j]));

        buf_idx_ = 0;
        out_idx_ = 0;
    }

    // The generator is mono; the dry note keeps the source's stereo image.
    const float voices = tone_.Process(out_buf_[out_idx_++]) * swell;
    return {input.left * dry_level_ + voices, input.right * dry_level_ + voices};
}

} // namespace pedal
